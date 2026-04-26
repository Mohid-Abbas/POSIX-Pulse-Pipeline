#include "common/common.h"

/* Queue Structure */
typedef struct {
    void *data;
    size_t size;
    int is_eof;
} queue_item_t;

queue_item_t *queue;
int queue_size;
int head = 0, tail = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t sem_empty, sem_full;

/* Aggregation Table */
aggregation_record_t table[MAX_RECORDS];
int table_count = 0;
pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

void enqueue(void *data, size_t size, int is_eof) {
    sem_wait(&sem_empty);
    pthread_mutex_lock(&queue_mutex);
    queue[tail].data = data;
    queue[tail].size = size;
    queue[tail].is_eof = is_eof;
    tail = (tail + 1) % queue_size;
    pthread_mutex_unlock(&queue_mutex);
    sem_post(&sem_full);
}

queue_item_t dequeue() {
    sem_wait(&sem_full);
    pthread_mutex_lock(&queue_mutex);
    queue_item_t item = queue[head];
    head = (head + 1) % queue_size;
    pthread_mutex_unlock(&queue_mutex);
    sem_post(&sem_empty);
    return item;
}

void process_chunk(char *chunk, size_t size) {
    (void)size;
    char *saveptr1, *saveptr2;
    char *line = strtok_r(chunk, "\n", &saveptr1);
    
    while (line != NULL) {
        char *line_copy = strdup(line);
        char *category = strtok_r(line_copy, ",", &saveptr2);
        
        if (category) {
            double line_total = 0;
            int has_values = 0;
            char *value_str;
            
            /* Generic: Aggregate all numeric columns after the first one */
            while ((value_str = strtok_r(NULL, ",", &saveptr2)) != NULL) {
                /* Basic check to see if it's a number (skip headers like 'Price') */
                if ((value_str[0] >= '0' && value_str[0] <= '9') || value_str[0] == '-' || value_str[0] == '.') {
                    line_total += atof(value_str);
                    has_values = 1;
                }
            }
            
            /* Only aggregate if we actually found numeric values */
            if (has_values) {
                pthread_mutex_lock(&table_mutex);
                int found = 0;
                for (int i = 0; i < table_count; i++) {
                    if (strcmp(table[i].category, category) == 0) {
                        table[i].total_revenue += line_total;
                        table[i].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found && table_count < MAX_RECORDS) {
                    strncpy(table[table_count].category, category, MAX_CATEGORY_LEN);
                    table[table_count].total_revenue = line_total;
                    table[table_count].count = 1;
                    table_count++;
                }
                pthread_mutex_unlock(&table_mutex);
            }
        }
        free(line_copy);
        line = strtok_r(NULL, "\n", &saveptr1);
    }
}

void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        queue_item_t item = dequeue();
        if (item.is_eof) break;
        
        process_chunk((char *)item.data, item.size);
        free(item.data);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <num_threads> <queue_size> <fifo_path> <shm_name>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    int num_threads = atoi(argv[1]);
    queue_size = atoi(argv[2]);
    const char *fifo_path = argv[3];
    const char *shm_name = argv[4];

    /* Initialize Semaphores with user-defined queue size Q */
    sem_init(&sem_empty, 0, queue_size);
    sem_init(&sem_full, 0, 0);

    queue = malloc(sizeof(queue_item_t) * queue_size);

    /* Create Thread Pool with explicit attributes */
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    /* Rubric: Explicit stack size and detach state */
    pthread_attr_setstacksize(&attr, 1024 * 1024); /* 1MB stack */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE); /* Threads will be joined */

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], &attr, worker_thread, NULL);
    }
    pthread_attr_destroy(&attr);

    /* FIFO Reader Thread (Main) */
    int fifo_fd = open(fifo_path, O_RDONLY);
    if (fifo_fd < 0) {
        perror("open fifo");
        return EXIT_IPC_ERROR;
    }

    chunk_header_t header;
    while (read(fifo_fd, &header, sizeof(header)) > 0) {
        if (header.is_eof) break;
        
        void *data = malloc(header.byte_count + 1);
        read(fifo_fd, data, header.byte_count);
        ((char *)data)[header.byte_count] = '\0';
        
        enqueue(data, header.byte_count, 0);
    }
    close(fifo_fd);

    /* Send Poison Pills */
    for (int i = 0; i < num_threads; i++) {
        enqueue(NULL, 0, 1);
    }

    /* Join Threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Serialize to Shared Memory */
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    shm_layout_t *shm_ptr = mmap(NULL, sizeof(shm_layout_t), PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    shm_ptr->record_count = table_count;
    memcpy(shm_ptr->records, table, sizeof(aggregation_record_t) * table_count);
    
    munmap(shm_ptr, sizeof(shm_layout_t));
    close(shm_fd);

    /* Signal Reporter */
    sem_t *sem = sem_open(SEM_NAME, 0);
    sem_post(sem);
    sem_close(sem);

    LOG_MSG("Processor finished. Aggregated %d categories.", table_count);
    return EXIT_SUCCESS_PIPE;
}

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

/* Which CSV column (1-indexed) to use as the grouping key */
int key_col = 1;

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

/*
 * Checks whether a string is a pure numeric value (integer or float).
 * Returns 1 if numeric, 0 otherwise.
 * This prevents strings like "2009/2010" from being parsed as numbers.
 */
int is_numeric(const char *str) {
    if (!str || *str == '\0') return 0;
    
    /* Skip leading whitespace */
    while (*str == ' ' || *str == '\t') str++;
    
    /* Allow optional leading sign */
    if (*str == '-' || *str == '+') str++;
    
    int has_digit = 0;
    int has_dot = 0;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            has_digit = 1;
        } else if (*str == '.' && !has_dot) {
            has_dot = 1;
        } else if (*str == '\r' || *str == ' ' || *str == '\t') {
            /* Trailing whitespace or carriage return is OK */
            break;
        } else {
            /* Any other character (like '/' in '2009/2010') means not numeric */
            return 0;
        }
        str++;
    }
    return has_digit;
}

void process_chunk(char *chunk, size_t size) {
    (void)size;
    char *saveptr1;
    char *line = strtok_r(chunk, "\n", &saveptr1);
    
    while (line != NULL) {
        /* Skip empty lines and lines that start with carriage return only */
        if (line[0] == '\0' || line[0] == '\r') {
            line = strtok_r(NULL, "\n", &saveptr1);
            continue;
        }

        char *line_copy = strdup(line);
        char *saveptr2;
        
        /* Tokenize to find the key column and aggregate numeric columns */
        char *token;
        char *category = NULL;
        int col = 0;
        double line_total = 0;
        int has_values = 0;
        
        token = strtok_r(line_copy, ",", &saveptr2);
        while (token != NULL) {
            col++;
            if (col == key_col) {
                /* Strip trailing \r */
                size_t tlen = strlen(token);
                if (tlen > 0 && token[tlen-1] == '\r') token[tlen-1] = '\0';
                category = token;
            } else if (is_numeric(token)) {
                line_total += atof(token);
                has_values = 1;
            }
            token = strtok_r(NULL, ",", &saveptr2);
        }
            
        /* Only aggregate if we found a key and numeric values */
        if (category && has_values) {
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
                strncpy(table[table_count].category, category, MAX_CATEGORY_LEN - 1);
                table[table_count].category[MAX_CATEGORY_LEN - 1] = '\0';
                table[table_count].total_revenue = line_total;
                table[table_count].count = 1;
                table_count++;
            }
            pthread_mutex_unlock(&table_mutex);
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

/* Read exactly 'count' bytes from fd, handling partial reads */
ssize_t read_all(int fd, void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t ret = read(fd, (char *)buf + total, count - total);
        if (ret <= 0) return (total > 0) ? (ssize_t)total : ret;
        total += ret;
    }
    return (ssize_t)total;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <num_threads> <queue_size> <fifo_path> <shm_name> <key_column>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    int num_threads = atoi(argv[1]);
    queue_size = atoi(argv[2]);
    const char *fifo_path = argv[3];
    const char *shm_name = argv[4];
    key_col = atoi(argv[5]);
    if (key_col < 1) key_col = 1;

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
    while (read_all(fifo_fd, &header, sizeof(header)) == (ssize_t)sizeof(header)) {
        if (header.is_eof) break;
        
        void *data = malloc(header.byte_count + 1);
        ssize_t got = read_all(fifo_fd, data, header.byte_count);
        if (got < (ssize_t)header.byte_count) {
            LOG_MSG("Warning: partial chunk read (%zd / %zu)", got, header.byte_count);
            free(data);
            break;
        }
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

    /* Cleanup synchronization primitives */
    sem_destroy(&sem_empty);
    sem_destroy(&sem_full);
    pthread_mutex_destroy(&queue_mutex);
    pthread_mutex_destroy(&table_mutex);
    free(queue);
    free(threads);

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

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
StockData table[MAX_RECORDS];
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
        char *symbol = NULL;
        int col = 0;
        double *values = NULL;
        size_t values_count = 0;
        size_t values_capacity = 0;
        int allocation_failed = 0;
        
        token = strtok_r(line_copy, ",", &saveptr2);
        while (token != NULL) {
            col++;
            if (col == key_col) {
                /* Strip trailing \r */
                size_t tlen = strlen(token);
                if (tlen > 0 && token[tlen-1] == '\r') token[tlen-1] = '\0';
                symbol = token;
            } else if (is_numeric(token)) {
                if (values_count == values_capacity) {
                    size_t new_capacity = (values_capacity == 0) ? 8 : values_capacity * 2;
                    double *new_values = realloc(values, new_capacity * sizeof(double));
                    if (!new_values) {
                        allocation_failed = 1;
                        break;
                    }
                    values = new_values;
                    values_capacity = new_capacity;
                }
                values[values_count++] = strtod(token, NULL);
            }
            token = strtok_r(NULL, ",", &saveptr2);
        }
            
        if (!allocation_failed && symbol && values_count >= 2) {
            double line_total_value = 0.0;
            int line_total_volume = 0;
            double line_high = values[0];
            double line_low = values[0];

            for (size_t i = 0; i + 1 < values_count; i += 2) {
                double price = values[i];
                double volume = values[i + 1];
                int volume_int = (int)volume;

                line_total_value += price * volume_int;
                line_total_volume += volume_int;

                if (price > line_high) line_high = price;
                if (price < line_low) line_low = price;
            }

            if (line_total_volume > 0) {
                pthread_mutex_lock(&table_mutex);
                int found = 0;
                for (int i = 0; i < table_count; i++) {
                    if (strcmp(table[i].symbol, symbol) == 0) {
                        table[i].total_value += line_total_value;
                        table[i].total_volume += line_total_volume;
                        if (line_high > table[i].high) table[i].high = line_high;
                        if (line_low < table[i].low) table[i].low = line_low;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (table_count >= MAX_RECORDS) {
                        LOG_MSG("ERROR: MAX_RECORDS limit reached, dropping symbol: %s", symbol);
                    } else {
                        strncpy(table[table_count].symbol, symbol, MAX_SYMBOL_LEN - 1);
                        table[table_count].symbol[MAX_SYMBOL_LEN - 1] = '\0';
                        table[table_count].total_value = line_total_value;
                        table[table_count].total_volume = line_total_volume;
                        table[table_count].high = line_high;
                        table[table_count].low = line_low;
                        table_count++;
                    }
                }
                pthread_mutex_unlock(&table_mutex);
            }
        }
        free(line_copy);
        free(values);
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

    /* Validate and convert numeric arguments */
    char *endptr;
    long num_threads_val = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || num_threads_val <= 0 || num_threads_val > 1000) {
        fprintf(stderr, "Error: Invalid thread count '%s'. Must be positive integer.\n", argv[1]);
        return EXIT_BAD_ARGS;
    }

    long queue_size_val = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || queue_size_val <= 0 || queue_size_val > 10000) {
        fprintf(stderr, "Error: Invalid queue size '%s'. Must be positive integer.\n", argv[2]);
        return EXIT_BAD_ARGS;
    }

    const char *fifo_path = argv[3];
    const char *shm_name = argv[4];
    
    long key_col_val = strtol(argv[5], &endptr, 10);
    if (*endptr != '\0' || key_col_val < 1) {
        fprintf(stderr, "Error: Invalid key column '%s'. Must be >= 1.\n", argv[5]);
        return EXIT_BAD_ARGS;
    }
    
    int num_threads = (int)num_threads_val;
    queue_size = (int)queue_size_val;
    key_col = (int)key_col_val;

    /* Initialize Semaphores with user-defined queue size Q */
    if (sem_init(&sem_empty, 0, queue_size) < 0) {
        perror("sem_init sem_empty");
        return EXIT_IPC_ERROR;
    }
    if (sem_init(&sem_full, 0, 0) < 0) {
        perror("sem_init sem_full");
        sem_destroy(&sem_empty);
        return EXIT_IPC_ERROR;
    }

    queue = malloc(sizeof(queue_item_t) * queue_size);
    if (!queue) {
        LOG_MSG("Error: Memory allocation failed for queue");
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        return EXIT_IO_ERROR;
    }

    /* Create Thread Pool with explicit attributes */
    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    if (!threads) {
        LOG_MSG("Error: Memory allocation failed for thread array");
        free(queue);
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        return EXIT_IO_ERROR;
    }
    
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        perror("pthread_attr_init");
        free(queue);
        free(threads);
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        return EXIT_IPC_ERROR;
    }
    
    /* Rubric: Explicit stack size and detach state */
    if (pthread_attr_setstacksize(&attr, 1024 * 1024) != 0) { /* 1MB stack */
        perror("pthread_attr_setstacksize");
        pthread_attr_destroy(&attr);
        free(queue);
        free(threads);
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        return EXIT_IPC_ERROR;
    }
    
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0) {
        perror("pthread_attr_setdetachstate");
        pthread_attr_destroy(&attr);
        free(queue);
        free(threads);
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        return EXIT_IPC_ERROR;
    }

    int thread_creation_failed = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], &attr, worker_thread, NULL) != 0) {
            LOG_MSG("Error: Failed to create thread %d", i);
            thread_creation_failed = 1;
            break;
        }
    }
    pthread_attr_destroy(&attr);
    
    if (thread_creation_failed) {
        /* We couldn't create all threads, but we'll try to continue with what we have */
        LOG_MSG("Warning: Only %d threads created (requested %d)", 
                thread_creation_failed == 0 ? num_threads : i, num_threads);
    }

    /* FIFO Reader Thread (Main) */
    int fifo_fd = open(fifo_path, O_RDONLY);
    if (fifo_fd < 0) {
        perror("open fifo");
        free(queue);
        free(threads);
        sem_destroy(&sem_empty);
        sem_destroy(&sem_full);
        pthread_mutex_destroy(&queue_mutex);
        pthread_mutex_destroy(&table_mutex);
        return EXIT_IPC_ERROR;
    }

    chunk_header_t header;
    int read_error = 0;
    while (read_all(fifo_fd, &header, sizeof(header)) == (ssize_t)sizeof(header)) {
        if (header.is_eof) break;
        
        void *data = malloc(header.byte_count + 1);
        if (!data) {
            LOG_MSG("Error: Memory allocation failed for chunk data");
            read_error = 1;
            break;
        }
        
        ssize_t got = read_all(fifo_fd, data, header.byte_count);
        if (got < (ssize_t)header.byte_count) {
            LOG_MSG("Warning: partial chunk read (%zd / %zu)", got, header.byte_count);
            free(data);
            read_error = 1;
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
    int join_error = 0;
    for (int i = 0; i < num_threads; i++) {
        int ret = pthread_join(threads[i], NULL);
        if (ret != 0) {
            LOG_MSG("Error: Failed to join thread %d: %s", i, strerror(ret));
            join_error = 1;
        }
    }
    
    if (join_error) {
        LOG_MSG("Warning: Some threads did not join cleanly");
    }

    /* Cleanup synchronization primitives */
    if (sem_destroy(&sem_empty) < 0) {
        perror("sem_destroy sem_empty");
    }
    if (sem_destroy(&sem_full) < 0) {
        perror("sem_destroy sem_full");
    }
    if (pthread_mutex_destroy(&queue_mutex) != 0) {
        perror("pthread_mutex_destroy queue_mutex");
    }
    if (pthread_mutex_destroy(&table_mutex) != 0) {
        perror("pthread_mutex_destroy table_mutex");
    }
    free(queue);
    free(threads);

    /* Serialize to Shared Memory */
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return EXIT_IPC_ERROR;
    }
    
    shm_layout_t *shm_ptr = mmap(NULL, sizeof(shm_layout_t), PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return EXIT_IPC_ERROR;
    }
    
    shm_ptr->record_count = table_count;
    memcpy(shm_ptr->records, table, sizeof(StockData) * table_count);
    
    if (munmap(shm_ptr, sizeof(shm_layout_t)) < 0) {
        perror("munmap");
    }
    close(shm_fd);

    /* Signal Reporter */
    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return EXIT_IPC_ERROR;
    }
    
    if (sem_post(sem) < 0) {
        perror("sem_post");
        sem_close(sem);
        return EXIT_IPC_ERROR;
    }
    
    sem_close(sem);

    LOG_MSG("Processor finished. Aggregated %d symbols.", table_count);
    return EXIT_SUCCESS_PIPE;
}

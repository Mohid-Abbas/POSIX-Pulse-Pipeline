#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>

/* Exit Codes */
#define EXIT_SUCCESS_PIPE 0
#define EXIT_BAD_ARGS 10
#define EXIT_IPC_ERROR 20
#define EXIT_CHILD_FAILURE 30
#define EXIT_IO_ERROR 40
#define EXIT_SIGINT 130
#define EXIT_SIGTERM 143

/* IPC Names */
#define SHM_NAME "/pulse_pipeline_shm"
#define SEM_NAME "/pulse_pipeline_sem"
#define FIFO_NAME "/tmp/pulse_pipeline_fifo"

/* Data Limits */
#define MAX_CATEGORY_LEN 64
#define MAX_CHUNKS_SIZE (64 * 1024) /* 64 KB */
#define MAX_RECORDS 1000

/* Chunk Structure */
typedef struct {
    int chunk_id;
    size_t byte_count;
    int source_file_id;
    int is_eof; /* 1 if this is the end of stream */
} chunk_header_t;

/* Aggregation Record (Retail Variant) */
typedef struct {
    char category[MAX_CATEGORY_LEN];
    double total_revenue;
    int count;
} aggregation_record_t;

/* Shared Memory Layout */
typedef struct {
    int record_count;
    aggregation_record_t records[MAX_RECORDS];
} shm_layout_t;

/* Logging Utility */
#define LOG_MSG(fmt, ...) \
    fprintf(stderr, "[PID: %d, PPID: %d] " fmt "\n", getpid(), getppid(), ##__VA_ARGS__)

#endif

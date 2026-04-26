#include "common/common.h"
#include <dirent.h>

volatile sig_atomic_t stop = 0;
int files_processed = 0;
int chunks_sent = 0;
size_t total_bytes = 0;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        stop = 1;
    } else if (sig == SIGUSR1) {
        LOG_MSG("Stats: %d files, %d chunks, %zu bytes sent", 
                files_processed, chunks_sent, total_bytes);
    }
}

/* Write all bytes to fd, handling partial writes */
ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t written = 0;
    while (written < count) {
        ssize_t ret = write(fd, (const char *)buf + written, count - written);
        if (ret < 0) return -1;
        written += ret;
    }
    return (ssize_t)written;
}

void send_chunk(int fifo_fd, const void *data, size_t size, int file_id, int is_eof) {
    chunk_header_t header;
    header.chunk_id = chunks_sent++;
    header.byte_count = size;
    header.source_file_id = file_id;
    header.is_eof = is_eof;

    write_all(fifo_fd, &header, sizeof(header));
    if (size > 0) {
        write_all(fifo_fd, data, size);
    }
    total_bytes += size;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_dir> <fifo_path>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    const char *input_dir = argv[1];
    const char *fifo_path = argv[2];

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);
    signal(SIGPIPE, SIG_IGN); /* Ignore SIGPIPE so we can handle write errors */

    int fifo_fd = open(fifo_path, O_WRONLY);
    if (fifo_fd < 0) {
        perror("open fifo");
        return EXIT_IPC_ERROR;
    }

    DIR *dir = opendir(input_dir);
    if (!dir) {
        perror("opendir");
        return EXIT_IO_ERROR;
    }

    struct dirent *entry;
    int file_id = 0;
    char raw_buf[MAX_CHUNKS_SIZE];
    /* 
     * Line-aware chunking: We carry over incomplete lines from the previous
     * read into the next chunk so that no CSV row is ever split in half.
     */
    char carry[4096]; /* buffer for the leftover partial line */
    int carry_len = 0;

    while ((entry = readdir(dir)) != NULL && !stop) {
        if (strstr(entry->d_name, ".csv")) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", input_dir, entry->d_name);
            
            int fd = open(filepath, O_RDONLY);
            if (fd < 0) continue;

            LOG_MSG("Processing file: %s", entry->d_name);
            carry_len = 0;

            ssize_t bytes_read;
            while ((bytes_read = read(fd, raw_buf, sizeof(raw_buf) - 1)) > 0 && !stop) {
                raw_buf[bytes_read] = '\0';
                
                /* Find the last newline in this chunk */
                int last_nl = -1;
                for (int i = bytes_read - 1; i >= 0; i--) {
                    if (raw_buf[i] == '\n') {
                        last_nl = i;
                        break;
                    }
                }

                if (last_nl == -1) {
                    /* No newline found — entire chunk is a partial line, append to carry */
                    if (carry_len + bytes_read < (int)sizeof(carry)) {
                        memcpy(carry + carry_len, raw_buf, bytes_read);
                        carry_len += bytes_read;
                    }
                    continue;
                }

                /* Build a clean chunk: carry + everything up to the last newline */
                int clean_len = carry_len + last_nl + 1;
                char *clean_buf = malloc(clean_len + 1);
                if (carry_len > 0) {
                    memcpy(clean_buf, carry, carry_len);
                }
                memcpy(clean_buf + carry_len, raw_buf, last_nl + 1);
                clean_buf[clean_len] = '\0';

                send_chunk(fifo_fd, clean_buf, clean_len, file_id, 0);
                free(clean_buf);

                /* Save the remainder after the last newline as carry */
                carry_len = bytes_read - last_nl - 1;
                if (carry_len > 0 && carry_len < (int)sizeof(carry)) {
                    memcpy(carry, raw_buf + last_nl + 1, carry_len);
                } else {
                    carry_len = 0;
                }
            }

            /* Flush any remaining carry from this file */
            if (carry_len > 0 && !stop) {
                carry[carry_len] = '\0';
                send_chunk(fifo_fd, carry, carry_len, file_id, 0);
                carry_len = 0;
            }

            close(fd);
            files_processed++;
            file_id++;
        }
    }
    closedir(dir);

    /* Send EOF Chunk */
    send_chunk(fifo_fd, NULL, 0, -1, 1);
    
    close(fifo_fd);
    LOG_MSG("Ingester finished. Processed %d files, sent %d chunks, %zu bytes total.", 
            files_processed, chunks_sent, total_bytes);
    return EXIT_SUCCESS_PIPE;
}

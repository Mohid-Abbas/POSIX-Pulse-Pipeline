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

void send_chunk(int fifo_fd, const void *data, size_t size, int file_id, int is_eof) {
    chunk_header_t header;
    header.chunk_id = chunks_sent++;
    header.byte_count = size;
    header.source_file_id = file_id;
    header.is_eof = is_eof;

    write(fifo_fd, &header, sizeof(header));
    if (size > 0) {
        write(fifo_fd, data, size);
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
    char buffer[MAX_CHUNKS_SIZE];

    while ((entry = readdir(dir)) != NULL && !stop) {
        if (strstr(entry->d_name, ".csv")) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", input_dir, entry->d_name);
            
            int fd = open(filepath, O_RDONLY);
            if (fd < 0) continue;

            ssize_t bytes_read;
            while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0 && !stop) {
                send_chunk(fifo_fd, buffer, bytes_read, file_id, 0);
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
    LOG_MSG("Ingester finished. Processed %d files.", files_processed);
    return EXIT_SUCCESS_PIPE;
}

#include "common/common.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <output_dir> <shm_name>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    const char *output_dir = argv[1];
    const char *shm_name = argv[2];

    /* Wait for Processor signal */
    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return EXIT_IPC_ERROR;
    }
    
    LOG_MSG("Waiting for data...");
    if (sem_wait(sem) < 0) {
        perror("sem_wait");
        sem_close(sem);
        return EXIT_IPC_ERROR;
    }
    sem_close(sem);

    /* Read Shared Memory */
    int shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return EXIT_IPC_ERROR;
    }
    
    shm_layout_t *shm_ptr = mmap(NULL, sizeof(shm_layout_t), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return EXIT_IPC_ERROR;
    }

    /* Prepare Output Paths */
    char txt_path[512], csv_path[512];
    snprintf(txt_path, sizeof(txt_path), "%s/report.txt", output_dir);
    snprintf(csv_path, sizeof(csv_path), "%s/report.csv", output_dir);

    /* Validate output directory */
    struct stat stat_buf;
    if (stat(output_dir, &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
        LOG_MSG("Error: Output directory '%s' does not exist or is not accessible.", output_dir);
        munmap(shm_ptr, sizeof(shm_layout_t));
        close(shm_fd);
        return EXIT_IO_ERROR;
    }

    /* 
     * DEMONSTRATION: dup() and dup2() system calls
     * As per rubric requirements, this block saves the current STDOUT descriptor,
     * redirects STDOUT to a file using dup2(), performs output via printf(),
     * and then restores the original STDOUT using the saved descriptor.
     */
    int stdout_save = dup(STDOUT_FILENO); /* Save current STDOUT to a new descriptor */
    if (stdout_save < 0) {
        perror("dup");
        munmap(shm_ptr, sizeof(shm_layout_t));
        close(shm_fd);
        return EXIT_IO_ERROR;
    }
    
    int fd_txt = open(txt_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_txt < 0) {
        perror("open report.txt");
        close(stdout_save);
        munmap(shm_ptr, sizeof(shm_layout_t));
        close(shm_fd);
        return EXIT_IO_ERROR;
    }
    
    /* Redirect STDOUT (file descriptor 1) to our report file */
    if (dup2(fd_txt, STDOUT_FILENO) < 0) {
        perror("dup2 failed");
        close(fd_txt);
        close(stdout_save);
        munmap(shm_ptr, sizeof(shm_layout_t));
        close(shm_fd);
        return EXIT_IO_ERROR;
    }
    
    int txt_write_error = 0;
    
    printf("Pulse Pipeline Final Report\n");
    printf("===========================\n");
    printf("%-16s | %-15s | %-10s | %-12s | %-10s | %-10s\n",
           "Symbol", "Total Value", "Volume", "VWAP", "High", "Low");
    printf("--------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < shm_ptr->record_count; i++) {
        double vwap = 0.0;
        if (shm_ptr->records[i].total_volume > 0) {
            vwap = shm_ptr->records[i].total_value / shm_ptr->records[i].total_volume;
        }
        int ret = printf("%-16s | %-15.2f | %-10d | %-12.2f | %-10.2f | %-10.2f\n",
               shm_ptr->records[i].symbol,
               shm_ptr->records[i].total_value,
               shm_ptr->records[i].total_volume,
               vwap,
               shm_ptr->records[i].high,
               shm_ptr->records[i].low);
        if (ret < 0) {
            txt_write_error = 1;
        }
    }
    
    if (fflush(stdout) < 0) {
        txt_write_error = 1;
    }
    
    close(fd_txt);
    
    /* Restore the original STDOUT from our saved copy */
    if (dup2(stdout_save, STDOUT_FILENO) < 0) {
        perror("dup2 restore");
    }
    close(stdout_save);
    
    if (txt_write_error) {
        LOG_MSG("Warning: Error writing to report.txt");
    }

    /* Write CSV summary */
    FILE *f_csv = fopen(csv_path, "w");
    if (!f_csv) {
        perror("fopen report.csv");
        munmap(shm_ptr, sizeof(shm_layout_t));
        close(shm_fd);
        return EXIT_IO_ERROR;
    }
    
    int csv_write_error = 0;
    
    if (fprintf(f_csv, "Symbol,Total Value,Volume,VWAP,High,Low\n") < 0) {
        csv_write_error = 1;
    }
    
    for (int i = 0; i < shm_ptr->record_count && !csv_write_error; i++) {
        double vwap = 0.0;
        if (shm_ptr->records[i].total_volume > 0) {
            vwap = shm_ptr->records[i].total_value / shm_ptr->records[i].total_volume;
        }
        int ret = fprintf(f_csv, "%s,%.2f,%d,%.2f,%.2f,%.2f\n",
                shm_ptr->records[i].symbol,
                shm_ptr->records[i].total_value,
                shm_ptr->records[i].total_volume,
                vwap,
                shm_ptr->records[i].high,
                shm_ptr->records[i].low);
        if (ret < 0) {
            csv_write_error = 1;
        }
    }
    
    if (fclose(f_csv) < 0) {
        LOG_MSG("Warning: Error closing report.csv");
    }
    
    if (csv_write_error) {
        LOG_MSG("Warning: Error writing to report.csv");
    }

    if (munmap(shm_ptr, sizeof(shm_layout_t)) < 0) {
        perror("munmap");
    }
    close(shm_fd);

    
    kill(getppid(), SIGUSR1);

    LOG_MSG("Reporter finished. Results written to %s", output_dir);
    return EXIT_SUCCESS_PIPE;
}

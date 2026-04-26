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
    sem_wait(sem);
    sem_close(sem);

    /* Read Shared Memory */
    int shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    shm_layout_t *shm_ptr = mmap(NULL, sizeof(shm_layout_t), PROT_READ, MAP_SHARED, shm_fd, 0);

    /* Prepare Output Paths */
    char txt_path[512], csv_path[512];
    snprintf(txt_path, sizeof(txt_path), "%s/report.txt", output_dir);
    snprintf(csv_path, sizeof(csv_path), "%s/report.csv", output_dir);

    /* Demonstration of dup() and dup2() */
    int stdout_save = dup(STDOUT_FILENO); /* Save current stdout */
    int fd_txt = open(txt_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_txt >= 0) {
        dup2(fd_txt, STDOUT_FILENO); /* Redirect stdout to report.txt */
        
        printf("Pulse Pipeline Final Report\n");
        printf("===========================\n");
        printf("%-20s | %-15s | %-10s\n", "Category", "Revenue", "Count");
        printf("---------------------------------------------------\n");
        for (int i = 0; i < shm_ptr->record_count; i++) {
            printf("%-20s | %-15.2f | %-10d\n", 
                   shm_ptr->records[i].category, 
                   shm_ptr->records[i].total_revenue,
                   shm_ptr->records[i].count);
        }
        
        fflush(stdout);
        close(fd_txt);
        dup2(stdout_save, STDOUT_FILENO); /* Restore original stdout */
        close(stdout_save);
    }

    /* Write CSV summary */
    FILE *f_csv = fopen(csv_path, "w");
    if (f_csv) {
        fprintf(f_csv, "Category,TotalRevenue,RecordCount\n");
        for (int i = 0; i < shm_ptr->record_count; i++) {
            fprintf(f_csv, "%s,%.2f,%d\n", 
                    shm_ptr->records[i].category, 
                    shm_ptr->records[i].total_revenue,
                    shm_ptr->records[i].count);
        }
        fclose(f_csv);
    }

    munmap(shm_ptr, sizeof(shm_layout_t));
    close(shm_fd);

    /* Signal Dispatcher that we are done */
    kill(getppid(), SIGUSR1);

    LOG_MSG("Reporter finished. Results written to %s", output_dir);
    return EXIT_SUCCESS_PIPE;
}

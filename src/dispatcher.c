#include "common/common.h"

volatile sig_atomic_t children_alive = 0;

pid_t ingester_pid, processor_pid, reporter_pid;

void signal_handler(int sig) {

    if (sig == SIGCHLD) {

        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

            children_alive--;

            int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            LOG_MSG("Child %d terminated with status %d", pid, exit_status);
        }
    } 
    else if (sig == SIGINT || sig == SIGTERM) {

        LOG_MSG("Shutting down... sending SIGTERM to children");

        if (ingester_pid > 0){
            kill(ingester_pid, SIGTERM);
        } 
        if (processor_pid > 0){
             kill(processor_pid, SIGTERM);
        }

        if (reporter_pid > 0){
         kill(reporter_pid, SIGTERM);
        }

    } 

    else if (sig == SIGUSR1) {
        LOG_MSG("Status: Reporter has finished report generation.");
    }
}

void setup_logging(const char *name) {

    char log_path[256];

    snprintf(log_path, sizeof(log_path), "logs/%s.log", name);
    int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        perror("open log");
        exit(EXIT_IO_ERROR);
    }
    
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 8) {
        fprintf(stderr, "Usage: %s <input_dir> <output_dir> <threads> <queue_size> <fifo_path> <shm_name> <key_column>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    const char *input_dir = argv[1];
    const char *output_dir = argv[2];
    const char *threads_str = argv[3];
    const char *queue_size_str = argv[4];
    const char *fifo_path = argv[5];
    const char *shm_name = argv[6];
    const char *key_col_str = argv[7];

    /* Validate numeric arguments */
    char *endptr;
    long threads_val = strtol(threads_str, &endptr, 10);
    if (*endptr != '\0' || threads_val <= 0 || threads_val > 1000) {
        fprintf(stderr, "Error: Invalid thread count '%s'. Must be positive integer.\n", threads_str);
        return EXIT_BAD_ARGS;
    }

    long queue_size_val = strtol(queue_size_str, &endptr, 10);
    if (*endptr != '\0' || queue_size_val <= 0 || queue_size_val > 10000) {
        fprintf(stderr, "Error: Invalid queue size '%s'. Must be positive integer.\n", queue_size_str);
        return EXIT_BAD_ARGS;
    }

    long key_col_val = strtol(key_col_str, &endptr, 10);
    if (*endptr != '\0' || key_col_val < 1) {
        fprintf(stderr, "Error: Invalid key column '%s'. Must be >= 1.\n", key_col_str);
        return EXIT_BAD_ARGS;
    }

    /* Validate directories exist */
    struct stat stat_buf;
    if (stat(input_dir, &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
        fprintf(stderr, "Error: Input directory '%s' does not exist or is not a directory.\n", input_dir);
        return EXIT_BAD_ARGS;
    }

    if (stat(output_dir, &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
        fprintf(stderr, "Error: Output directory '%s' does not exist or is not a directory.\n", output_dir);
        return EXIT_BAD_ARGS;
    }

    /* Initialize Signal Handlers */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction SIGCHLD");
        return EXIT_IPC_ERROR;
    }
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction SIGINT");
        return EXIT_IPC_ERROR;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction SIGTERM");
        return EXIT_IPC_ERROR;
    }
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction SIGUSR1");
        return EXIT_IPC_ERROR;
    }

    /* Create IPC Resources - with cleanup on failure */
    if (mkfifo(fifo_path, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return EXIT_IPC_ERROR;
    }

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        unlink(fifo_path);
        return EXIT_IPC_ERROR;
    }

    if (ftruncate(shm_fd, sizeof(shm_layout_t)) < 0) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(shm_name);
        unlink(fifo_path);
        return EXIT_IPC_ERROR;
    }

    /* Named Semaphore for Reporter synchronization */
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 0);
    if (sem == SEM_FAILED) {
        if (errno == EEXIST) {
            sem_unlink(SEM_NAME);
            sem = sem_open(SEM_NAME, O_CREAT, 0666, 0);
        }
        if (sem == SEM_FAILED) {
            perror("sem_open");
            close(shm_fd);
            shm_unlink(shm_name);
            unlink(fifo_path);
            return EXIT_IPC_ERROR;
        }
    }
    sem_close(sem);

    /* Fork Reporter */
    reporter_pid = fork();
    if (reporter_pid < 0) {
        perror("fork reporter");
        shm_unlink(shm_name);
        unlink(fifo_path);
        sem_unlink(SEM_NAME);
        return EXIT_IPC_ERROR;
    }
    if (reporter_pid == 0) {
        setup_logging("reporter");
        char *args[] = {"./reporter", (char *)output_dir, (char *)shm_name, NULL};
        execvp(args[0], args);
        perror("exec reporter");
        exit(EXIT_CHILD_FAILURE);
    }
    children_alive++;

    /* Fork Processor */
    processor_pid = fork();
    if (processor_pid < 0) {
        perror("fork processor");
        kill(reporter_pid, SIGTERM);
        shm_unlink(shm_name);
        unlink(fifo_path);
        sem_unlink(SEM_NAME);
        return EXIT_IPC_ERROR;
    }
    if (processor_pid == 0) {
        setup_logging("processor");
        char threads_arg[32], queue_arg[32], key_arg[32];
        snprintf(threads_arg, sizeof(threads_arg), "%ld", threads_val);
        snprintf(queue_arg, sizeof(queue_arg), "%ld", queue_size_val);
        snprintf(key_arg, sizeof(key_arg), "%ld", key_col_val);
        char *args[] = {"./processor", threads_arg, queue_arg, (char *)fifo_path, (char *)shm_name, key_arg, NULL};
        execvp(args[0], args);
        perror("exec processor");
        exit(EXIT_CHILD_FAILURE);
    }
    children_alive++;

    /* Fork Ingester */
    ingester_pid = fork();
    if (ingester_pid < 0) {
        perror("fork ingester");
        kill(reporter_pid, SIGTERM);
        kill(processor_pid, SIGTERM);
        shm_unlink(shm_name);
        unlink(fifo_path);
        sem_unlink(SEM_NAME);
        return EXIT_IPC_ERROR;
    }
    if (ingester_pid == 0) {
        setup_logging("ingester");
        char *args[] = {"./ingester", (char *)input_dir, (char *)fifo_path, NULL};
        execvp(args[0], args);
        perror("exec ingester");
        exit(EXIT_CHILD_FAILURE);
    }
    children_alive++;

    LOG_MSG("Dispatcher started. Children: Ingester(%d), Processor(%d), Reporter(%d)", 
            ingester_pid, processor_pid, reporter_pid);

    /* sigsuspend loop */
    sigset_t mask;
    sigemptyset(&mask);
    int exit_status = EXIT_SUCCESS_PIPE;
    
    while (children_alive > 0) {
        sigsuspend(&mask);
    }

    /* Cleanup - ensure ALL resources are freed on every exit path */
    LOG_MSG("Cleaning up IPC resources...");
    
    /* Kill any remaining children that didn't exit cleanly */
    if (ingester_pid > 0) {
        if (kill(ingester_pid, 0) == 0) {
            kill(ingester_pid, SIGKILL);
            waitpid(ingester_pid, NULL, 0);
        }
    }
    if (processor_pid > 0) {
        if (kill(processor_pid, 0) == 0) {
            kill(processor_pid, SIGKILL);
            waitpid(processor_pid, NULL, 0);
        }
    }
    if (reporter_pid > 0) {
        if (kill(reporter_pid, 0) == 0) {
            kill(reporter_pid, SIGKILL);
            waitpid(reporter_pid, NULL, 0);
        }
    }
    
    /* Unlink IPC resources */
    if (unlink(fifo_path) < 0 && errno != ENOENT) {
        LOG_MSG("Warning: Failed to unlink FIFO %s", fifo_path);
    }
    if (shm_unlink(shm_name) < 0 && errno != ENOENT) {
        LOG_MSG("Warning: Failed to unlink shared memory %s", shm_name);
    }
    if (sem_unlink(SEM_NAME) < 0 && errno != ENOENT) {
        LOG_MSG("Warning: Failed to unlink semaphore %s", SEM_NAME);
    }
    
    close(shm_fd);

    LOG_MSG("All children finished. Dispatcher exiting with status %d.", exit_status);
    return exit_status;
}

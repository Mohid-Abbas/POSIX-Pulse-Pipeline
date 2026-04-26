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
    } else if (sig == SIGINT || sig == SIGTERM) {
        LOG_MSG("Shutting down... sending SIGTERM to children");
        if (ingester_pid > 0) kill(ingester_pid, SIGTERM);
        if (processor_pid > 0) kill(processor_pid, SIGTERM);
        if (reporter_pid > 0) kill(reporter_pid, SIGTERM);
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
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <input_dir> <output_dir> <threads> <fifo_path> <shm_name>\n", argv[0]);
        return EXIT_BAD_ARGS;
    }

    const char *input_dir = argv[1];
    const char *output_dir = argv[2];
    const char *threads = argv[3];
    const char *fifo_path = argv[4];
    const char *shm_name = argv[5];

    /* Initialize Signal Handlers */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Create IPC Resources */
    if (mkfifo(fifo_path, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return EXIT_IPC_ERROR;
    }

    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return EXIT_IPC_ERROR;
    }
    ftruncate(shm_fd, sizeof(shm_layout_t));

    /* Named Semaphore for Reporter synchronization */
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 0);
    if (sem == SEM_FAILED) {
        if (errno == EEXIST) {
            sem_unlink(SEM_NAME);
            sem = sem_open(SEM_NAME, O_CREAT, 0666, 0);
        } else {
            perror("sem_open");
            return EXIT_IPC_ERROR;
        }
    }
    sem_close(sem);

    /* Fork Reporter */
    reporter_pid = fork();
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
    if (processor_pid == 0) {
        setup_logging("processor");
        char *args[] = {"./processor", (char *)threads, (char *)fifo_path, (char *)shm_name, NULL};
        execvp(args[0], args);
        perror("exec processor");
        exit(EXIT_CHILD_FAILURE);
    }
    children_alive++;

    /* Fork Ingester */
    ingester_pid = fork();
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
    while (children_alive > 0) {
        sigsuspend(&mask);
    }

    /* Cleanup */
    unlink(fifo_path);
    shm_unlink(shm_name);
    sem_unlink(SEM_NAME);

    LOG_MSG("All children finished. Dispatcher exiting.");
    return EXIT_SUCCESS_PIPE;
}

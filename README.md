# Parallel CSV Data Processing Pipeline (POSIX-Pulse-Pipeline)

## Project Overview
This project implements a high-performance, parallel data processing pipeline on Linux. It is designed to ingest CSV datasets, process them concurrently using a pool of worker threads, aggregate the results, and generate structured reports. The system is built entirely using C/C++ POSIX system calls, bypassing high-level frameworks to demonstrate direct interaction with the operating system primitives.

The current implementation uses the **Retail Transactions** dataset variant, aggregating total revenue and record counts grouped by product category.

### Key Operating System Concepts Demonstrated:
- **Process Management**: Heavy use of `fork()`, `execvp()`, `waitpid()`, and `getpid()`/`getppid()`.
- **Inter-Process Communication (IPC)**: Data streams between processes using Named Pipes (FIFOs) and Shared Memory segments (`shm_open`, `mmap`).
- **Multi-Threading**: Concurrent CSV chunk processing using a pool of `pthread` workers.
- **Synchronization**: Producer-consumer queue management using Semaphores (`sem_t`) and Mutexes (`pthread_mutex_t`).
- **Signal Handling**: Orchestrated graceful shutdown and status reporting via `SIGINT`, `SIGTERM`, `SIGCHLD`, and `SIGUSR1`.
- **File Descriptor Manipulation**: Demonstration of stream redirection using `dup()` and `dup2()`.

---

## System Architecture

The pipeline consists of four cooperating C programs, orchestrated by a master Bash script:

1. **Dispatcher (`dispatcher`)**: The master orchestrator. It sets up the IPC infrastructure (FIFO, Shared Memory, Semaphores), spawns the other three processes as children, manages signals, and waits for everything to complete gracefully.
2. **Ingester (`ingester`)**: Scans the input directory for CSV files, reads them into memory chunks, and pushes these chunks down the Named Pipe (FIFO) towards the processor.
3. **Processor (`processor`)**: The heavy lifter. It runs a dedicated reader thread to pull chunks from the FIFO into a bounded queue. A pool of worker threads consumes this queue, parsing the CSV data and safely updating a shared aggregation table using mutexes. Upon completion, it serializes the final table into POSIX Shared Memory.
4. **Reporter (`reporter`)**: Waits on a named semaphore until the Processor finishes. It then reads the Shared Memory segment, generates both a human-readable `report.txt` and a machine-readable `report.csv`, and signals the Dispatcher that the job is done.

---

## How to Build and Run

### Prerequisites
- A Linux environment (Ubuntu 22.04 or later recommended)
- `gcc` compiler
- `make` utility
- `bash` shell

### 1. Project Setup
Ensure all your CSV data files are placed inside the `data/` directory. A `sample.csv` is provided by default.
The required format is: `Category,Revenue` (e.g., `Electronics,1200.50`).

### 2. Compilation
You can compile the project manually using `make`, but the orchestration script handles this automatically. To compile manually:
```bash
make clean
make
```

### 3. Execution via Orchestrator
The `run.sh` script is the primary entry point. It builds the binaries, sets up directories, and launches the dispatcher.

**Make the script executable (if it isn't already):**
```bash
chmod +x run.sh
```

**Run the pipeline with default settings:**
```bash
./run.sh
```

**Run with custom configurations:**
```bash
./run.sh -i data -o output -n 4 -c
```

**Command-Line Options for `run.sh`:**
- `-i <dir>` : Specify the input directory containing `.csv` files (Default: `data`).
- `-o <dir>` : Specify the output directory for reports (Default: `output`).
- `-n <num>` : Set the number of worker threads in the processor (Default: `4`).
- `-c`       : Perform a clean build (`make clean`) before compiling.
- `-h`       : Show the help message.

---

## Understanding the Output

Once the pipeline finishes successfully (Status `0`), check the following locations:

### 1. Final Reports (`output/` directory)
- **`report.txt`**: A formatted, human-readable summary table of the aggregated data.
- **`report.csv`**: A machine-readable CSV version of the aggregated data (Category, TotalRevenue, RecordCount).

### 2. Process Logs (`logs/` directory)
During execution, the Dispatcher uses `dup2()` to redirect the standard output and standard error of its child processes to log files. 
- `logs/ingester.log`
- `logs/processor.log`
- `logs/reporter.log`

Look inside these files to see trace messages, including the PID and PPID of each component.

---

## Clean Shutdown and Robustness
You can interrupt the pipeline at any time by pressing `Ctrl+C` (`SIGINT`). The Dispatcher intercepts this signal and propagates a `SIGTERM` to all child processes. 
The components are programmed to catch this, cleanly release resources, unlink FIFOs, unmap Shared Memory, and exit without leaving zombie processes or memory leaks. You can verify this by running `ipcs -m` or checking `/tmp/` after an aborted run.

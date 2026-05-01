# POSIX Pulse Pipeline
### Parallel Multi-Process, Multi-Threaded CSV Data Engine on Linux

## 🚀 Overview
The **POSIX Pulse Pipeline** is a high-performance data processing system built entirely using C and low-level Linux system calls. It demonstrates core operating system concepts including process management, inter-process communication (IPC), multithreading, and synchronization.

The system is designed to ingest large CSV datasets concurrently, process them through a thread pool, and generate aggregated reports.

## 🏗 Architecture
The system is divided into four distinct components:
1. **Dispatcher (Master)**: Orchestrates the pipeline, manages child processes (`fork`/`exec`), and handles IPC lifecycle.
2. **Ingester**: Reads CSV data and streams it through a **Named Pipe (FIFO)**.
3. **Processor**: Implements a **Thread Pool** with a **Bounded Buffer**. Aggregates data into **Shared Memory**.
4. **Reporter**: Reads results from Shared Memory and generates final human/machine-readable reports.

## 🛠 Features & OS Concepts
- **Concurrency**: Thread pool using `pthread` with configurable workers ($N$).
- **Synchronization**: Bounded buffer managed with **Semaphores** (empty/full) and **Mutexes**.
- **IPC**: Named Pipes (FIFOs) for data streaming and POSIX Shared Memory for result handoff.
- **Signals**: Graceful shutdown using `SIGINT`/`SIGTERM` and status reporting with `SIGUSR1`.
- **I/O Redirection**: Child logging implemented via `dup2()`, and `stdout` restoration demonstrated with `dup()`.
- **Shell Orchestration**: Master `run.sh` script with robust error handling and configuration.

## 📂 Project Structure
```text
.
├── Makefile                # Builds all components
├── run.sh                  # Master orchestration script
├── README.md               # This file
├── DECLARATION.txt         # Academic honesty statement
├── src/                    # Source code
│   ├── dispatcher.c        # Process management
│   ├── ingester.c          # Data streaming
│   ├── processor.c         # Thread pool & aggregation
│   ├── reporter.c          # Report generation
│   └── common/             # Shared headers & IPC structures
├── data/                   # Input datasets (*.csv)
├── logs/                   # Per-process log files
└── output/                 # Final reports (report.txt, report.csv)
```

## 🚀 Getting Started

### Prerequisites
- Linux (Ubuntu 22.04+ recommended)
- `gcc`, `make`
- `bash`

### Compilation
Build the entire pipeline using the master script or Makefile:
```bash
make clean && make
```

### Running the Pipeline
Use the `run.sh` orchestrator for a full execution:
```bash
./run.sh -i data -o output -n 4 -q 10 -k 1
```

**Options:**
- `-i`: Input directory (default: `data`)
- `-o`: Output directory (default: `output`)
- `-n`: Number of worker threads (default: `4`)
- `-q`: Queue size for bounded buffer (default: `10`)
- `-k`: CSV column to use as grouping key (default: `1`)
- `-c`: Perform a clean build before running

## 📊 Testing the Bounded Buffer (Queue)
To demonstrate the robustness of the synchronization logic, you can generate a large dataset and run with a small queue:
1. Generate data: `./data/generate_data.sh`
2. Run with small queue: `./run.sh -q 5 -n 8`
3. Verify `logs/processor.log` for correct handling of backpressure.

## 📜 Academic Honesty
This project was developed for **CS-2006: Operating Systems**. See `DECLARATION.txt` for the signed statement of original work.

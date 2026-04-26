# POSIX Pulse Pipeline

A parallel CSV data processing pipeline implemented using POSIX system calls on Linux.

## Features
- **Multi-Process Architecture**: Dispatcher, Ingester, Processor, and Reporter.
- **IPC**: Communication via Named Pipes (FIFOs) and Shared Memory.
- **Multi-Threading**: Processor uses a thread pool with a bounded buffer.
- **Synchronization**: Mutexes and Semaphores (Named and Unnamed).
- **Signal Handling**: Graceful shutdown and status dumps.

## Prerequisites
- Linux (Ubuntu 22.04+ recommended)
- `gcc`, `make`, `bash`

## Quick Start
1. Place CSV files in the `data/` directory.
2. Run the pipeline:
   ```bash
   ./run.sh -i data -o output -n 4
   ```
3. Check the reports in the `output/` directory:
   - `report.txt`: Human-readable summary.
   - `report.csv`: Machine-readable data.

## Command-Line Options
- `-i`: Input directory (default: `data`)
- `-o`: Output directory (default: `output`)
- `-n`: Number of worker threads (default: `4`)
- `-c`: Clean build before running
- `-h`: Show help

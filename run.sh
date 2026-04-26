#!/bin/bash

# Configuration defaults
INPUT_DIR="data"
OUTPUT_DIR="output"
THREADS=4
CLEAN=0
FIFO_PATH="/tmp/pulse_pipeline_fifo"
SHM_NAME="/pulse_pipeline_shm"

usage() {
    echo "Usage: $0 [-i input_dir] [-o output_dir] [-n num_threads] [-c] [-h]"
    echo "  -i  Directory containing CSV files (default: data)"
    echo "  -o  Directory for reports (default: output)"
    echo "  -n  Number of worker threads (default: 4)"
    echo "  -c  Clean build before running"
    echo "  -h  Display this help message"
    exit 1
}

# Parse options
while getopts "i:o:n:ch" opt; do
    case ${opt} in
        i) INPUT_DIR=$OPTARG ;;
        o) OUTPUT_DIR=$OPTARG ;;
        n) THREADS=$OPTARG ;;
        c) CLEAN=1 ;;
        h) usage ;;
        *) usage ;;
    esac
done

# Cleanup function for trap
cleanup() {
    echo "Shutting down..."
    if [ -f .pid ]; then
        PID=$(cat .pid)
        kill -TERM "$PID" 2>/dev/null
        rm .pid
    fi
}

trap cleanup EXIT INT TERM

# Build project
if [ $CLEAN -eq 1 ]; then
    make clean
fi

if ! make; then
    echo "Build failed!"
    exit 1
fi

# Prepare directories
mkdir -p "$OUTPUT_DIR"
mkdir -p logs

# Verify input
if [ ! -d "$INPUT_DIR" ] || [ -z "$(ls -A "$INPUT_DIR"/*.csv 2>/dev/null)" ]; then
    echo "Error: Input directory '$INPUT_DIR' not found or contains no CSV files."
    exit 40
fi

echo "Launching Pulse Pipeline..."
echo "Input: $INPUT_DIR | Output: $OUTPUT_DIR | Threads: $THREADS"

# Start dispatcher
./dispatcher "$INPUT_DIR" "$OUTPUT_DIR" "$THREADS" "$FIFO_PATH" "$SHM_NAME" &
DISPATCHER_PID=$!
echo $DISPATCHER_PID > .pid

# Wait for dispatcher
wait $DISPATCHER_PID
STATUS=$?

echo "Pipeline finished with status $STATUS"

# Final summary
if [ -f "$OUTPUT_DIR/report.csv" ]; then
    RECORDS=$(tail -n +2 "$OUTPUT_DIR/report.csv" | cut -d',' -f3 | awk '{s+=$1} END {print s}')
    echo "Total records processed: $RECORDS"
fi

exit $STATUS

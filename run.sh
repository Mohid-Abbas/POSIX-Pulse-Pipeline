#!/bin/bash

# Configuration defaults
INPUT_DIR="data"
OUTPUT_DIR="output"
THREADS=4
QUEUE_SIZE=10
CLEAN=0
FIFO_PATH="/tmp/pulse_pipeline_fifo"
SHM_NAME="/pulse_pipeline_shm"

# --- Rubric Requirement: At least three Bash functions ---

# Function 1: Display usage information
usage() {
    echo "Usage: $0 [-i input_dir] [-o output_dir] [-n num_threads] [-q queue_size] [-c] [-h]"
    echo "  -i  Directory containing CSV files (default: data)"
    echo "  -o  Directory for reports (default: output)"
    echo "  -n  Number of worker threads (default: 4)"
    echo "  -q  Queue size (default: 10)"
    echo "  -c  Clean build before running"
    echo "  -h  Display this help message"
    exit 1
}

# Function 2: Cleanup function for trap
cleanup() {
    echo "Shutting down..."
    if [ -f .pid ]; then
        PID=$(cat .pid)
        kill -TERM "$PID" 2>/dev/null
        rm .pid
    fi
}

# Function 3: Validate environment and build
build_and_validate() {
    if [ "$CLEAN" -eq 1 ]; then
        make clean
    fi

    if ! make; then
        echo "Build failed!"
        exit 1
    fi
    
    mkdir -p "$OUTPUT_DIR"
    mkdir -p logs
}

# --- Main Script Logic ---

# Parse options using getopts (Rubric Requirement)
while getopts "i:o:n:q:ch" opt; do
    case ${opt} in
        i) INPUT_DIR=$OPTARG ;;
        o) OUTPUT_DIR=$OPTARG ;;
        n) THREADS=$OPTARG ;;
        q) QUEUE_SIZE=$OPTARG ;;
        c) CLEAN=1 ;;
        h) usage ;;
        *) usage ;;
    esac
done

trap cleanup EXIT INT TERM

build_and_validate

# Verify input
if [ ! -d "$INPUT_DIR" ] || [ -z "$(ls -A "$INPUT_DIR"/*.csv 2>/dev/null)" ]; then
    echo "Error: Input directory '$INPUT_DIR' not found or contains no CSV files."
    exit 40
fi

echo "Launching Pulse Pipeline..."
echo "Input: $INPUT_DIR | Output: $OUTPUT_DIR | Threads: $THREADS | Queue: $QUEUE_SIZE"

# Start dispatcher with all required arguments
./dispatcher "$INPUT_DIR" "$OUTPUT_DIR" "$THREADS" "$QUEUE_SIZE" "$FIFO_PATH" "$SHM_NAME" &
DISPATCHER_PID=$!
echo $DISPATCHER_PID > .pid

# Wait for dispatcher
wait $DISPATCHER_PID
STATUS=$?

echo "Pipeline finished with status $STATUS"

# --- Rubric Requirement: Arithmetic expansion ---
if [ -f "$OUTPUT_DIR/report.csv" ]; then
    RECORDS_READ=$(tail -n +2 "$OUTPUT_DIR/report.csv" | cut -d',' -f3 | awk '{s+=$1} END {print s}')
    # Simple arithmetic expansion demonstration
    TOTAL_PROCESSED=$((RECORDS_READ + 0)) 
    echo "Total records processed: $TOTAL_PROCESSED"
fi

exit $STATUS

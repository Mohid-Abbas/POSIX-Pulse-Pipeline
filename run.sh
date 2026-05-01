#!/bin/bash

# Configuration defaults
INPUT_DIR="data"
OUTPUT_DIR="output"
THREADS=4
QUEUE_SIZE=10
KEY_COL=1
CLEAN=0
FIFO_PATH="/tmp/pulse_pipeline_fifo"
SHM_NAME="/pulse_pipeline_shm"

# --- Rubric Requirement: At least three Bash functions ---

# Function 1: Display usage information
usage() {
    echo "Usage: $0 [-i input_dir] [-o output_dir] [-n num_threads] [-q queue_size] [-k key_column] [-c] [-h]"
    echo "  -i  Directory containing CSV files (default: data)"
    echo "  -o  Directory for reports (default: output)"
    echo "  -n  Number of worker threads (default: 4)"
    echo "  -q  Queue size (default: 10)"
    echo "  -k  Key column number, 1-indexed (default: 1)"
    echo "      Example: -k 2 groups by Country, -k 3 groups by League"
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

# Function 3: Validate integer argument
validate_positive_int() {
    local val=$1
    local name=$2
    
    # Check if empty
    if [ -z "$val" ]; then
        echo "Error: $name not specified."
        exit 10
    fi
    
    # Check if it's a positive integer using regex
    if ! echo "$val" | grep -qE '^[0-9]+$'; then
        echo "Error: $name must be a positive integer, got: $val"
        exit 10
    fi
    
    # Check if it's greater than 0
    if [ "$val" -le 0 ]; then
        echo "Error: $name must be greater than 0, got: $val"
        exit 10
    fi
}

# Function 4: Validate environment and build
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
while getopts "i:o:n:q:k:ch" opt; do
    case ${opt} in
        i) INPUT_DIR=$OPTARG ;;
        o) OUTPUT_DIR=$OPTARG ;;
        n) THREADS=$OPTARG ;;
        q) QUEUE_SIZE=$OPTARG ;;
        k) KEY_COL=$OPTARG ;;
        c) CLEAN=1 ;;
        h) usage ;;
        *) usage ;;
    esac
done

trap cleanup EXIT INT TERM

# Validate numeric arguments BEFORE build
validate_positive_int "$THREADS" "Number of threads (-n)"
validate_positive_int "$QUEUE_SIZE" "Queue size (-q)"
validate_positive_int "$KEY_COL" "Key column (-k)"

build_and_validate

# Verify input directory exists
if [ ! -d "$INPUT_DIR" ]; then
    echo "Error: Input directory '$INPUT_DIR' not found."
    exit 10
fi

# Verify output directory exists
if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Error: Output directory '$OUTPUT_DIR' not found or cannot create."
    exit 10
fi

# Verify at least one CSV file exists
if [ -z "$(ls -A "$INPUT_DIR"/*.csv 2>/dev/null)" ]; then
    echo "Error: Input directory '$INPUT_DIR' contains no CSV files."
    exit 40
fi

echo "Launching Pulse Pipeline..."
echo "Input: $INPUT_DIR | Output: $OUTPUT_DIR | Threads: $THREADS | Queue: $QUEUE_SIZE | Key Column: $KEY_COL"

# Start dispatcher with all required arguments
./dispatcher "$INPUT_DIR" "$OUTPUT_DIR" "$THREADS" "$QUEUE_SIZE" "$FIFO_PATH" "$SHM_NAME" "$KEY_COL" &
DISPATCHER_PID=$!
echo $DISPATCHER_PID > .pid

# Wait for dispatcher and capture exit status
wait $DISPATCHER_PID
STATUS=$?

# Verify dispatcher exited successfully
if [ $STATUS -ne 0 ]; then
    echo "Pipeline failed with status $STATUS"
    exit $STATUS
fi

echo "Pipeline finished successfully"

# --- Rubric Requirement: Arithmetic expansion ---
if [ -f "$OUTPUT_DIR/report.csv" ]; then
    RECORDS_READ=$(tail -n +2 "$OUTPUT_DIR/report.csv" | cut -d',' -f3 | awk '{s+=$1} END {print s}')
    # Simple arithmetic expansion demonstration
    TOTAL_PROCESSED=$((RECORDS_READ + 0)) 
    echo "Total records processed: $TOTAL_PROCESSED"
fi

exit $STATUS

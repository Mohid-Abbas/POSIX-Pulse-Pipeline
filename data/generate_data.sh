#!/bin/bash

# Script to generate test CSV data for the POSIX Pulse Pipeline
DATA_DIR="data"
NUM_FILES=5
ROWS_PER_FILE=1000

mkdir -p "$DATA_DIR"

CATEGORIES=("Electronics" "Clothing" "Groceries" "Home" "Automotive" "Garden")

for i in $(seq 1 $NUM_FILES); do
    FILE="$DATA_DIR/test_data_$i.csv"
    echo "Generating $FILE..."
    rm -f "$FILE"
    for j in $(seq 1 $ROWS_PER_FILE); do
        CAT=${CATEGORIES[$RANDOM % ${#CATEGORIES[@]}]}
        REV=$(echo "scale=2; $RANDOM/100" | bc)
        echo "$CAT,$REV" >> "$FILE"
    done
done

echo "Successfully generated $NUM_FILES files with $ROWS_PER_FILE rows each in $DATA_DIR/"

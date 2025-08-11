#!/bin/bash

echo "Building ETL Exception System Test..."

# Create build directory if it doesn't exist
mkdir -p build_exceptions

# Compile the exception system
echo "Compiling exception system..."
g++ -std=c++17 -Wall -Wextra -O2 -I./include \
    src/etl_exceptions.cpp \
    scripts/test_etl_exceptions.cpp \
    -o build_exceptions/test_etl_exceptions

if [ $? -eq 0 ]; then
    echo "✓ Compilation successful!"
    echo ""
    echo "Running tests..."
    echo "================"
    ./build_exceptions/test_etl_exceptions
else
    echo "✗ Compilation failed!"
    exit 1
fi
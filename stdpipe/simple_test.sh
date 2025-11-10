#!/bin/bash

echo "Testing C++ stdpipe_back controlling stdpipe_serv..."

# Run the C++ version (executables are in the same directory as this script when copied to build/)
./stdpipe_back ./stdpipe_serv

echo "Test completed!"
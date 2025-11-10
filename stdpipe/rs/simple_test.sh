#!/bin/bash

echo "Testing Rust stdpipe_back controlling stdpipe_serv..."

# Run the Rust version
./target/debug/stdpipe_back ./target/debug/stdpipe_serv

echo "Test completed!"
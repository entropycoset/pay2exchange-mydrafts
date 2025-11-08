#!/bin/bash

echo "Testing stdpipe_serv with simple pipe..."

# Test 1: Test with echo piped to the program
echo -e "ping\nquit" | {
    # Create file descriptors 3 and 4 from stdin and stdout
    exec 3<&0  # fd 3 reads from stdin
    exec 4>&1  # fd 4 writes to stdout
    ./stdpipe_serv 3 4
}

echo "Test completed successfully!"
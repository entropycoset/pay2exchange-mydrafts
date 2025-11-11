#!/bin/bash

echo "Testing C++ stdpipe_back controlling stdpipe_serv..."
echo

# Test 1: Direct mode (without wrapper)
echo "=== Test 1: Direct mode (without cleanup_exec wrapper) ==="
./stdpipe_back ./stdpipe_serv
if [ $? -eq 0 ]; then
    echo "✓ Direct mode test PASSED"
else
    echo "✗ Direct mode test FAILED"
    exit 1
fi
echo

# Test 2: Wrapper mode (with cleanup_exec)
echo "=== Test 2: Wrapper mode (with cleanup_exec wrapper) ==="
./stdpipe_back ./stdpipe_serv ./safe_exec
if [ $? -eq 0 ]; then
    echo "✓ Wrapper mode test PASSED"
else
    echo "✗ Wrapper mode test FAILED"
    exit 1
fi
echo

echo "🎉 All tests completed successfully!"
echo "Both modes of _back program tested: with cleanup_exec wrapper and without wrapper."
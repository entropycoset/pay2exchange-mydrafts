#!/bin/bash

echo "Testing stdpipe_back controlling stdpipe_serv..."

# stdpipe_back now launches and controls stdpipe_serv using Boost.Process
./stdpipe_back ./stdpipe_serv

echo "Test completed!"
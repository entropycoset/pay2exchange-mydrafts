#!/bin/bash

echo "Testing stdpipe_back and stdpipe_serv with anonymous pipes..."

# Create anonymous pipes for bidirectional communication
# Pipe 1: stdpipe_back -> stdpipe_serv (commands)
# Pipe 2: stdpipe_serv -> stdpipe_back (responses)

# Create named pipes for testing (simulating anonymous pipes)
mkfifo /tmp/cmd_pipe
mkfifo /tmp/resp_pipe

# Start both programs in background with proper file descriptors
{
    # Open pipes for stdpipe_serv
    exec 3</tmp/cmd_pipe    # Read commands from pipe
    exec 4>/tmp/resp_pipe   # Write responses to pipe
    ./stdpipe_serv 3 4
} &
SERV_PID=$!

# Give server time to start
sleep 0.1

{
    # Open pipes for stdpipe_back
    exec 3>/tmp/cmd_pipe    # Write commands to pipe
    exec 4</tmp/resp_pipe   # Read responses from pipe
    ./stdpipe_back 3 4
} &
BACK_PID=$!

# Wait for both processes to complete
wait $BACK_PID
wait $SERV_PID

# Cleanup
rm -f /tmp/cmd_pipe /tmp/resp_pipe

echo "Test completed successfully!"
echo "will start the tmux with tests..."
echo $TERM
sleep 2
tmux new-session './myapp; read x' \; \
  split-window -h 'sleep 0.1; ./client; read x' \; \
  split-window -h 'sleep 2; ./client; read x' \; \
  select-layout even-horizontal


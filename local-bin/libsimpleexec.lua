-- local posix = require("posix")
local unistd  = require("posix.unistd")
local signal  = require("posix.signal")
local wait    = require("posix.sys.wait")

local posix = {
  fork      = unistd.fork,
  execp     = unistd.execp,
  setpgid   = unistd.setpgid,
  tcgetpgrp = unistd.tcgetpgrp,
  tcsetpgrp = unistd.tcsetpgrp,
  _exit     = unistd._exit,

  kill      = signal.kill,
  signal    = signal.signal,

  wait      = wait.wait,
}

local THISLIB = {}

function THISLIB.simple_exec(prog, execargs, to_front)
  to_front = not not to_front

  local argv = { [0] = prog }
  for i = 1, #execargs do argv[i] = execargs[i] end

  local parent_pgrp
  if to_front then
    parent_pgrp = posix.tcgetpgrp(0)
  end

  local pid = posix.fork()
  if pid == 0 then
    if to_front then
      posix.setpgid(0, 0)
      posix.tcsetpgrp(0, posix.getpid())
    end
    posix.execp(prog, argv)
    io.stderr:write("Failed to exec: ", prog, "\n")
    posix._exit(127)
  end

  local old_int, old_tstp
  if to_front then
    local function forward(sig) posix.kill(pid, sig) end
    old_int  = posix.signal(signal.SIGINT,  forward)
    old_tstp = posix.signal(signal.SIGTSTP, forward)

    posix.setpgid(pid, pid)
    posix.tcsetpgrp(0, pid)
  end

  local why, status
  repeat
    _, why, status = posix.wait(pid)
  until why ~= "signal" or status ~= signal.SIGINT

  if to_front then
    posix.tcsetpgrp(0, parent_pgrp or unistd.getpid())
    posix.signal(signal.SIGINT,  old_int)
    posix.signal(signal.SIGTSTP, old_tstp)
  end

  if why == "exited" then
    io.write(("Child exited with code %d\n"):format(status))
  elseif why == "signal" then
    io.write(("Child killed by signal %d\n"):format(status))
  else
    io.write(("Child ended: %s (%d)\n"):format(why, status))
  end
end

return THISLIB


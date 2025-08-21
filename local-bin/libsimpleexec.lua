local posix = require("posix")

local THISLIB = {}

function THISLIB.simple_exec(prog, execargs, to_front)
  to_front = not not to_front

  -- build argv
  local argv = { [0] = prog }
  for i = 1, #execargs do
    argv[i] = execargs[i]
  end

  -- grab our TTY pgrp so we can restore it later
  local parent_pgrp
  if to_front then
    parent_pgrp = posix.tcgetpgrp(0)
  end

  local pid = posix.fork()
  if pid == 0 then
    -- ───────── Child ─────────
    if to_front then
      posix.setpgid(0, 0)                    -- child → new pgid
      posix.tcsetpgrp(0, posix.getpid())     -- give it the terminal
    end

    posix.execp(prog, argv)
    io.stderr:write("Failed to exec: ", prog, "\n")
    posix._exit(127)

  else
    -- ───────── Parent ─────────
    local old_int, old_tstp

    if to_front then
      -- forward SIGINT and SIGTSTP to the child
      local function forward(sig)
        posix.kill(pid, sig)
      end

      old_int  = posix.signal(posix.SIGINT,  forward)
      old_tstp = posix.signal(posix.SIGTSTP, forward)

      posix.setpgid(pid, pid)
      posix.tcsetpgrp(0, pid)
    end

    -- wait (retry if interrupted by a signal we caught)
    local why, status
    repeat
      _, why, status = posix.wait(pid)
    until why ~= "signal" or status ~= posix.SIGINT

    -- restore terminal & handlers
    if to_front then
      posix.tcsetpgrp(0, parent_pgrp or posix.getpid())
      posix.signal(posix.SIGINT,  old_int)
      posix.signal(posix.SIGTSTP, old_tstp)
    end

    -- report exit
    if why == "exited" then
      io.write(("Child exited with code %d\n"):format(status))
    elseif why == "signal" then
      io.write(("Child killed by signal %d\n"):format(status))
    else
      io.write(("Child ended: %s (%d)\n"):format(why, status))
    end
  end
end

return THISLIB


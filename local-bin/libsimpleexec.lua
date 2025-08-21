LIB={}
-- ffi_simple_exec.lua

local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
// types & constants
  typedef int         pid_t;
  typedef unsigned long sigset_t;
  enum {
    STDIN_FILENO = 0,
    SIGINT       = 2,
    SIGTSTP      = 20,
    SIGTTOU      = 22,
    SA_RESTART   = 0x10000000
  };

// process control
  pid_t fork(void);
  int   setpgid(pid_t pid, pid_t pgid);
  pid_t getpid(void);

// terminal control
  pid_t tcgetpgrp(int fd);
  int   tcsetpgrp(int fd, pid_t pgrp);

// exec & wait
  int   execvp(const char *file, char *const argv[]);
  pid_t waitpid(pid_t pid, int *status, int options);
  void  _exit(int status);

// signal handling
  struct sigaction {
    void       (*sa_handler)(int);
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
  };
  int sigaction(int signum,
                const struct sigaction *act,
                struct sigaction *oldact);
  int kill(pid_t pid, int sig);

// errno
  extern int errno;
]]

-- waitpid status helpers
local function WIFEXITED(s)   return bit.band(s, 0x7f) == 0 end
local function WEXITSTATUS(s) return bit.rshift(bit.band(s, 0xff00), 8) end
local function WIFSIGNALED(s) return bit.band(s,0x7f) ~= 0  and bit.band(s,0x7f) ~= 0x7f end
local function WTERMSIG(s)    return bit.band(s, 0x7f) end

-- keep callbacks alive so they are never GC’d
local _signal_anchors = {}

function LIB.simple_exec(prog, args, to_front)
  to_front = not not to_front

  -- build a NULL‐terminated const char* argv
  local argc = #args + 1
  local argv = ffi.new("const char *[?]", argc+1)
  argv[0] = prog
  for i=1,#args do argv[i] = args[i] end
  argv[argc] = nil

  -- save our current foreground pgrp (so we can restore later)
  local parent_pgrp
  if to_front then
    parent_pgrp = ffi.C.tcgetpgrp(0)
  end

  -- fork off the child
  local pid = ffi.C.fork()
  if pid == 0 then
    -- ─── Child ───
    if to_front then
      if ffi.C.setpgid(0,0) < 0 then
        io.stderr:write("setpgid failed: ", ffi.errno(), "\n")
        ffi.C._exit(127)
      end
      ffi.C.tcsetpgrp(0, ffi.C.getpid())
    end

    ffi.C.execvp(prog, ffi.cast("char *const *", argv))
    -- only reach here if exec failed
    io.stderr:write("execvp failed: ", ffi.errno(), "\n")
    ffi.C._exit(127)
  end

  -- ─── Parent ───
  -- prepare slots for old handlers
  local old_int  = ffi.new("struct sigaction[1]")
  local old_tstp = ffi.new("struct sigaction[1]")
  local old_ttou = ffi.new("struct sigaction[1]")

  if to_front then
    -- 1) Ignore SIGTTOU so we aren't stopped when changing the terminal
    local ign = ffi.new("struct sigaction[1]")
    -- SIG_IGN is (void(*)(int))1 in libc
    ign[0].sa_handler = ffi.cast("void(*)(int)", 1)
    ign[0].sa_mask    = 0
    ign[0].sa_flags   = ffi.C.SA_RESTART
    ffi.C.sigaction(ffi.C.SIGTTOU, ign, old_ttou)

    -- 2) Forward SIGINT & SIGTSTP to the child
    local cb = ffi.cast("void(*)(int)", function(sig)
      ffi.C.kill(pid, sig)
    end)
    _signal_anchors[#_signal_anchors+1] = cb

    local sa = ffi.new("struct sigaction[1]")
    sa[0].sa_handler = cb
    sa[0].sa_mask    = 0
    sa[0].sa_flags   = ffi.C.SA_RESTART

    ffi.C.sigaction(ffi.C.SIGINT,  sa, old_int)
    ffi.C.sigaction(ffi.C.SIGTSTP, sa, old_tstp)

    -- 3) Ensure the child’s process group exists, then hand over the TTY
    ffi.C.setpgid(pid, pid)
    ffi.C.tcsetpgrp(0, pid)
  end

  -- wait for the child
  local st = ffi.new("int[1]")
  ffi.C.waitpid(pid, st, 0)
  local status = st[0]

  -- restore our terminal & signal handlers
  if to_front then
    ffi.C.tcsetpgrp(0, parent_pgrp or ffi.C.getpid())
    ffi.C.sigaction(ffi.C.SIGINT,  old_int,  nil)
    ffi.C.sigaction(ffi.C.SIGTSTP, old_tstp, nil)
    ffi.C.sigaction(ffi.C.SIGTTOU, old_ttou, nil)
  end

  -- print exit status
  if WIFEXITED(status) then
    io.write(("Child exited with code %d\n"):format(WEXITSTATUS(status)))
  elseif WIFSIGNALED(status) then
    io.write(("Child killed by signal %d\n"):format(WTERMSIG(status)))
  else
    io.write(("Child ended with status 0x%x\n"):format(status))
  end
end

return LIB

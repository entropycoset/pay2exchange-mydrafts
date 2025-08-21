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
    SA_RESTART   = 0x10000000,
    SIG_IGN      = 1          // define the ignore‐signal handler constant
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

// simple signal(2) & kill(2)
  typedef void (*sighandler_t)(int);
  sighandler_t signal(int signum, sighandler_t handler);
  int          kill(pid_t pid, int sig);

// errno
  extern int errno;
]]

-- waitpid‐status decoding
local function WIFEXITED(s)   return bit.band(s,0x7f)==0 end
local function WEXITSTATUS(s) return bit.rshift(bit.band(s,0xff00),8) end
local function WIFSIGNALED(s) return bit.band(s,0x7f)~=0 and bit.band(s,0x7f)~=0x7f end
local function WTERMSIG(s)    return bit.band(s,0x7f) end

-- keep callbacks alive
local _anchors = {}

local function simple_exec(prog, args, to_front)
  to_front = not not to_front

  -- build NULL‐terminated argv
  local argc = #args + 1
  local argv = ffi.new("const char *[?]", argc+1)
  argv[0] = prog
  for i=1,#args do argv[i] = args[i] end
  argv[argc] = nil

  -- save parent’s fg pgrp
  local parent_pgrp
  if to_front then
    parent_pgrp = ffi.C.tcgetpgrp(0)
  end

  -- fork
  local pid = ffi.C.fork()
  if pid == 0 then
    -- Child
    if to_front then
      if ffi.C.setpgid(0,0) < 0 then
        io.stderr:write("setpgid failed: ", ffi.errno(), "\n")
        ffi.C._exit(127)
      end
      ffi.C.tcsetpgrp(0, ffi.C.getpid())
    end

    ffi.C.execvp(prog,
      ffi.cast("char *const *", argv)
    )
    io.stderr:write("execvp failed: ", ffi.errno(), "\n")
    ffi.C._exit(127)
  end

  -- Parent: set up signal handling & terminal hand‐off
  local old_int, old_tstp, old_ttou

  if to_front then
    -- ignore SIGTTOU so parent isn’t stopped when we tcsetpgrp()
    old_ttou = ffi.C.signal(ffi.C.SIGTTOU,
                   ffi.cast("sighandler_t", ffi.C.SIG_IGN)
                 )

    -- forward SIGINT & SIGTSTP to child
    local cb = ffi.cast("sighandler_t", function(sig)
      ffi.C.kill(pid, sig)
    end)
    _anchors[#_anchors+1] = cb

    old_int  = ffi.C.signal(ffi.C.SIGINT,  cb)
    old_tstp = ffi.C.signal(ffi.C.SIGTSTP, cb)

    -- ensure child pgid then give it the terminal
    ffi.C.setpgid(pid, pid)
    ffi.C.tcsetpgrp(0, pid)
  end

  -- wait for child
  local st = ffi.new("int[1]")
  ffi.C.waitpid(pid, st, 0)
  local status = st[0]

  -- restore terminal & handlers
  if to_front then
    ffi.C.tcsetpgrp(0, parent_pgrp or ffi.C.getpid())
    ffi.C.signal(ffi.C.SIGINT,  old_int)
    ffi.C.signal(ffi.C.SIGTSTP, old_tstp)
    ffi.C.signal(ffi.C.SIGTTOU, old_ttou)
  end

  -- print exit status
  if WIFEXITED(status) then
    print(("Child exited with code %d"):format(WEXITSTATUS(status)))
  elseif WIFSIGNALED(status) then
    print(("Child killed by signal %d"):format(WTERMSIG(status)))
  else
    print(("Child ended with status 0x%x"):format(status))
  end
end

return { simple_exec = simple_exec }


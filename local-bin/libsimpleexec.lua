local THISLIB = {}

local unistd    = require("posix.unistd")      -- fork, execp, tcgetpgrp, tcsetpgrp, _exit
local ffi = require("ffi")
local bit = require("bit")   -- LuaJIT’s bitwise library

ffi.cdef[[
  typedef int pid_t;
  enum { STDIN_FILENO = 0 };

  pid_t fork(void);
  pid_t setsid(void);
  pid_t getpid(void);
  int   tcsetpgrp(int fd, pid_t pgrp);

  int   execvp(const char *file, char *const argv[]);
  pid_t waitpid(pid_t pid, int *status, int options);
  void  _exit(int status);

  // errno support
  extern int errno;
]]



local function WIFEXITED(s)   return bit.band(s, 0x7f) == 0 end
local function WEXITSTATUS(s) return bit.rshift(bit.band(s, 0xff00), 8) end
local function WIFSIGNALED(s) return bit.band(s, 0x7f) ~= 0 and bit.band(s, 0x7f) ~= 0x7f end
local function WTERMSIG(s)    return bit.band(s, 0x7f) end



local function setpgid(pid, pgid)
  local ok, err = syscall(SYS_setpgid, pid, pgid)
  if ok == -1 then error("setpgid syscall failed: "..tostring(err)) end
end



function THISLIB.simple_exec(prog, args, to_front)
  to_front = not not to_front

  -- build a NULL-terminated C array of char*
  local argc = #args + 1
  local argv = ffi.new("const char *[?]", argc + 1)
  argv[0] = prog
  for i = 1, #args do
    argv[i] = args[i]
  end
  argv[argc] = nil

  -- fork the child
  local pid = ffi.C.fork()
  if pid == 0 then
    -- ───── Child ─────
    if to_front then
      if ffi.C.setsid() < 0 then
        io.stderr:write("setsid failed: ", ffi.errno(), "\n")
        ffi.C._exit(127)
      end
      ffi.C.tcsetpgrp(ffi.C.STDIN_FILENO, ffi.C.getpid())
    end

    ffi.C.execvp(prog, ffi.cast("char *const *", argv))
    -- only reached on exec failure
    io.stderr:write("execvp failed: ", ffi.errno(), "\n")
    ffi.C._exit(127)
  end

  -- ───── Parent ─────
  local status_arr = ffi.new("int[1]")
  ffi.C.waitpid(pid, status_arr, 0)
  local status = status_arr[0]

  if WIFEXITED(status) then
    print(("Child exited with code %d"):format(WEXITSTATUS(status)))
  elseif WIFSIGNALED(status) then
    print(("Child killed by signal %d"):format(WTERMSIG(status)))
  else
    print(("Child ended with status 0x%x"):format(status))
  end
end



return THISLIB




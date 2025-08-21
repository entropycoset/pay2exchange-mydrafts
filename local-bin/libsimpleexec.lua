local ffi = require("ffi")
local bit = require("bit")
local C   = ffi.C

ffi.cdef[[
typedef int pid_t;
typedef long ssize_t;
typedef unsigned int   tcflag_t;
typedef unsigned char  cc_t;
typedef unsigned int   speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[32];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct pollfd {
    int   fd;
    short events;
    short revents;
};

typedef unsigned long nfds_t;

int   openpty(int *amaster, int *aslave, char *name,
              const struct termios *termp, const struct winsize *winp);
int   fork(void);
int   setsid(void);
int   ioctl(int fd, unsigned long request, ...);
int   dup2(int oldfd, int newfd);
int   close(int fd);
int   execvp(const char *file, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int   poll(struct pollfd *fds, nfds_t nfds, int timeout);
int   tcgetattr(int fd, struct termios *termios_p);
int   tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
void  cfmakeraw(struct termios *termios_p);
int   kill(pid_t pid, int sig);
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
]]

-- Lua constants
local TCSANOW   = 0
local WNOHANG   = 1
local SIGINT    = 2
local SIGWINCH  = 28

local POLLIN    = 0x0001
local POLLERR   = 0x0008
local POLLHUP   = 0x0010

local TIOCSCTTY  = 0x540E
local TIOCGWINSZ = 0x5413
local TIOCSWINSZ = 0x5414

-- maximum buffer size to avoid runaway memory usage
local MAX_BUF_SIZE = 1024*1024 -- 1 MB

-- build a C argv that stays alive
local function build_argv(list)
  local n = #list
  local arr = ffi.new("char *[?]", n + 1)
  local keep = {}
  for i = 1, n do
    local s = tostring(list[i])
    local c = ffi.new("char[?]", #s + 1)
    ffi.copy(c, s, #s)
    arr[i-1] = c
    keep[i] = c
  end
  arr[n] = nil
  return arr, keep
end

local function exit_code_from_status(st)
  return bit.band(bit.rshift(st, 8), 0xff)
end

local function simple_exec(cmd, args, _ignored)
  args = args or {}

  local argv_list = { "stdbuf", "-o0", "-e0", cmd }
  for i = 1, #args do argv_list[#argv_list+1] = args[i] end
  local argv, _keep = build_argv(argv_list)

  local master = ffi.new("int[1]")
  local slave  = ffi.new("int[1]")
  if C.openpty(master, slave, nil, nil, nil) ~= 0 then
    error("openpty failed")
  end

  local saved = ffi.new("struct termios[1]")
  local have_saved = (C.tcgetattr(0, saved) == 0)
  local raw = ffi.new("struct termios[1]")
  if have_saved then
    raw[0] = saved[0]
    C.cfmakeraw(raw)
    if C.tcsetattr(0, TCSANOW, raw) ~= 0 then
      C.close(master[0]); C.close(slave[0])
      error("tcsetattr(raw) failed")
    end
  end

  local SIG_IGN = ffi.cast("sighandler_t", 1)
  local old_sigint = C.signal(SIGINT, SIG_IGN)
  local child_pid

  -- robust cleanup function for TTY, signals, and child process
  local cleanup
  cleanup = function(msg, already_in_error)
    -- kill child if exists
    if child_pid and child_pid > 0 then
      pcall(C.kill, child_pid, SIGINT)
      local st = ffi.new("int[1]")
      pcall(C.waitpid, child_pid, st, WNOHANG)
    end
    -- restore terminal
    if have_saved then
      pcall(C.tcsetattr, 0, TCSANOW, saved)
    end
    -- restore SIGINT
    if old_sigint then
      pcall(C.signal, SIGINT, old_sigint)
    end
    -- report message if provided
    if msg then
      if already_in_error then
        io.stderr:write("simple_exec error: ", msg, "\n")
      else
        error(msg)
      end
    end
  end

  local ok, err = xpcall(function()
    local pid = C.fork()
    if pid < 0 then cleanup("fork failed") end
    if pid == 0 then
      -- Child process
      C.close(master[0])
      C.setsid()
      if C.ioctl(slave[0], TIOCSCTTY, 0) ~= 0 then os.exit(127) end
      -- propagate window size
      local ws = ffi.new("struct winsize[1]")
      if C.ioctl(0, TIOCGWINSZ, ws) == 0 then
        C.ioctl(slave[0], TIOCSWINSZ, ws)
      end
      C.dup2(slave[0], 0)
      C.dup2(slave[0], 1)
      C.dup2(slave[0], 2)
      C.close(slave[0])
      local SIG_DFL = ffi.cast("sighandler_t", 0)
      C.signal(SIGINT, SIG_DFL)
      C.execvp(argv[0], argv)
      os.exit(127)
    end

    child_pid = pid
    C.close(slave[0])

    local ws = ffi.new("struct winsize[1]")
    local winch_pending = false
    local function sigwinch_handler(signum)
      winch_pending = true
    end
    C.signal(SIGWINCH, ffi.cast("sighandler_t", sigwinch_handler))
    -- initial resize
    if C.ioctl(0, TIOCGWINSZ, ws) == 0 then
      C.ioctl(master[0], TIOCSWINSZ, ws)
    end

    local pfds = ffi.new("struct pollfd[2]")
    pfds[0].fd = 0;          pfds[0].events = POLLIN
    pfds[1].fd = master[0];  pfds[1].events = POLLIN

    -- enforce maximum buffer size
    local buf_size = 65536
    if buf_size > MAX_BUF_SIZE then
        cleanup(("buffer size %d exceeds maximum %d"):format(buf_size, MAX_BUF_SIZE))
    end
    local buf = ffi.new("uint8_t[?]", buf_size)

    local child_alive = true
    local exit_status = 0

    while child_alive do
      local pret = C.poll(pfds, 2, -1)
      if pret < 0 then cleanup("poll failed") end

      -- handle SIGWINCH deferred
      if winch_pending then
        winch_pending = false
        if C.ioctl(0, TIOCGWINSZ, ws) == 0 then
          C.ioctl(master[0], TIOCSWINSZ, ws)
        end
      end

      -- from child -> stdout
      if bit.band(pfds[1].revents, bit.bor(POLLIN, bit.bor(POLLERR, POLLHUP))) ~= 0 then
        local n = C.read(master[0], buf, buf_size)
        if n < 0 then cleanup("read(master) failed") end
        if n > MAX_BUF_SIZE then cleanup(("child output exceeded max buffer %d bytes"):format(MAX_BUF_SIZE)) end
        if n == 0 then
          child_alive = false
        else
          local written = 0
          while written < n do
            local w = C.write(1, buf + written, n - written)
            if w <= 0 then cleanup("write(stdout) failed") end
            written = written + w
          end
        end
      end

      -- from stdin -> child
      if bit.band(pfds[0].revents, POLLIN) ~= 0 then
        local n = C.read(0, buf, buf_size)
        if n < 0 then cleanup("read(stdin) failed") end
        if n > MAX_BUF_SIZE then cleanup(("stdin read exceeded max buffer %d bytes"):format(MAX_BUF_SIZE)) end
        if n > 0 then
          local written = 0
          while written < n do
            local w = C.write(master[0], buf + written, n - written)
            if w <= 0 then cleanup("write(master) failed") end
            written = written + w
          end
        end
      end

      local st = ffi.new("int[1]")
      local w = C.waitpid(child_pid, st, WNOHANG)
      if w == child_pid then
        exit_status = st[0]
        child_alive = false
      end
    end

    local st = ffi.new("int[1]")
    C.waitpid(child_pid, st, 0)
    if st[0] ~= 0 then exit_status = st[0] end

    return exit_code_from_status(exit_status)
  end, function(err)
    cleanup(err, true)
  end)

  if not ok then
    cleanup(err, true)
  end

  return ok
end

return { simple_exec = simple_exec }


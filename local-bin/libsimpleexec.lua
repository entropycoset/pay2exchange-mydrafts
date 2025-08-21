local ffi  = require("ffi")
local bit  = require("bit")
local C    = ffi.C

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

-- Constants (define in Lua, NOT inside cdef)
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

-- Build a C argv[] that stays alive
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

-- Return child's exit code from wait status
local function exit_code_from_status(st)
  -- POSIX: if exited normally, (st >> 8) & 0xff
  return bit.band(bit.rshift(st, 8), 0xff)
end

local function simple_exec(cmd, args, _ignored)
  args = args or {}

  -- Force unbuffered child I/O
  local argv_list = { "stdbuf", "-o0", "-e0", cmd }
  for i = 1, #args do argv_list[#argv_list+1] = args[i] end
  local argv, _keep = build_argv(argv_list)

  -- Create PTY
  local master = ffi.new("int[1]")
  local slave  = ffi.new("int[1]")
  if C.openpty(master, slave, nil, nil, nil) ~= 0 then
    error("openpty failed")
  end

  -- Save our terminal; set our stdin raw so we forward bytes immediately
  local saved = ffi.new("struct termios[1]")
  local have_saved = (C.tcgetattr(0, saved) == 0)
  local raw = ffi.new("struct termios[1]")
  if have_saved then
    raw[0] = saved[0]
    C.cfmakeraw(raw)
    C.tcsetattr(0, TCSANOW, raw)
  end

  -- Parent ignores SIGINT; child (on slave PTY) will receive ^C
  local SIG_IGN = ffi.cast("sighandler_t", 1)
  local old_sigint = C.signal(SIGINT, SIG_IGN)

  local pid = C.fork()
  if pid < 0 then
    if have_saved then C.tcsetattr(0, TCSANOW, saved) end
    C.signal(SIGINT, old_sigint)
    error("fork failed")
  elseif pid == 0 then
    -- ---- Child ----
    C.close(master[0])

    -- New session + give slave PTY as controlling terminal
    C.setsid()
    C.ioctl(slave[0], TIOCSCTTY, 0)

    -- Propagate our current window size
    local ws = ffi.new("struct winsize[1]")
    if C.ioctl(0, TIOCGWINSZ, ws) == 0 then
      C.ioctl(slave[0], TIOCSWINSZ, ws)
    end

    -- stdio -> slave
    C.dup2(slave[0], 0)
    C.dup2(slave[0], 1)
    C.dup2(slave[0], 2)
    C.close(slave[0])

    -- Child gets default SIGINT
    local SIG_DFL = ffi.cast("sighandler_t", 0)
    C.signal(SIGINT, SIG_DFL)

    C.execvp(argv[0], argv)
    -- If exec fails:
    os.exit(127)
  end

  -- ---- Parent ----
  C.close(slave[0])

  -- poll() both stdin (0) and PTY master
  local pfds = ffi.new("struct pollfd[2]")
  pfds[0].fd = 0;          pfds[0].events = POLLIN
  pfds[1].fd = master[0];  pfds[1].events = POLLIN

  local buf = ffi.new("uint8_t[65536]")

  local child_alive = true
  local exit_status = 0

  while child_alive do
    local pret = C.poll(pfds, 2, -1)
    if pret < 0 then break end

    -- From child -> to our stdout
    if bit.band(pfds[1].revents, bit.bor(POLLIN, bit.bor(POLLERR, POLLHUP))) ~= 0 then
      local n = C.read(master[0], buf, 65536)
      if n > 0 then
        C.write(1, buf, n)
      else
        -- EOF/HUP
        child_alive = false
      end
    end

    -- From our keyboard -> to child
    if bit.band(pfds[0].revents, POLLIN) ~= 0 then
      local n = C.read(0, buf, 65536)
      if n > 0 then
        C.write(master[0], buf, n)
      end
    end

    -- Non-blocking reap
    local st = ffi.new("int[1]")
    local w = C.waitpid(pid, st, WNOHANG)
    if w == pid then
      exit_status = st[0]
      child_alive = false
    end
  end

  -- Final reap in case we missed it
  local st = ffi.new("int[1]")
  C.waitpid(pid, st, 0)
  if st[0] ~= 0 then exit_status = st[0] end

  -- Cleanup/restore
  C.close(master[0])
  if have_saved then C.tcsetattr(0, TCSANOW, saved) end
  C.signal(SIGINT, old_sigint)

  return exit_code_from_status(exit_status)
end

return { simple_exec = simple_exec }



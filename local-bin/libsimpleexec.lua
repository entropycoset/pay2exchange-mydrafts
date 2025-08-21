local ffi = require("ffi")
local bit = require("bit")

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

static const int TCSANOW  = 0;
static const int WNOHANG  = 1;
static const int SIGINT   = 2;
static const int SIGWINCH = 28;

static const short POLLIN  = 0x0001;
static const short POLLERR = 0x0008;
static const short POLLHUP = 0x0010;

static const unsigned long TIOCSCTTY   = 0x540E;
static const unsigned long TIOCGWINSZ  = 0x5413;
static const unsigned long TIOCSWINSZ  = 0x5414;
]]

local C = ffi.C

-- build a C argv that stays alive
local function build_argv(list)
  local n = #list
  local arr = ffi.new("char *[?]", n + 1)
  local keep = {}
  for i = 1, n do
    local s = list[i]
    local c = ffi.new("char[?]", #s + 1)
    ffi.copy(c, s, #s)
    arr[i-1] = c
    keep[i] = c
  end
  arr[n] = nil
  return arr, keep
end

local function simple_exec(cmd, args, _ignored)
  args = args or {}

  -- Wrap with stdbuf to force unbuffered stdout/stderr from the child.
  local argv_list = { "stdbuf", "-o0", "-e0", cmd }
  for i = 1, #args do argv_list[#argv_list+1] = tostring(args[i]) end
  local argv, _keep = build_argv(argv_list)

  -- Create PTY
  local master = ffi.new("int[1]")
  local slave  = ffi.new("int[1]")
  if C.openpty(master, slave, nil, nil, nil) ~= 0 then
    error("openpty failed")
  end

  -- Save our terminal to restore later; set our stdin to raw so we forward keystrokes byte-by-byte.
  local orig_term = ffi.new("struct termios[1]")
  if C.tcgetattr(0, orig_term) == 0 then
    local raw = ffi.new("struct termios[1]", orig_term[0])
    C.cfmakeraw(raw)
    C.tcsetattr(0, C.TCSANOW, raw)
  end

  -- Ignore SIGINT in the parent; the child (which owns the PTY) should handle ^C.
  local SIG_IGN = ffi.cast("sighandler_t", 1)
  local old_sigint = C.signal(C.SIGINT, SIG_IGN)

  local pid = C.fork()
  if pid < 0 then
    -- restore on error
    C.tcsetattr(0, C.TCSANOW, orig_term)
    C.signal(C.SIGINT, old_sigint)
    error("fork failed")
  elseif pid == 0 then
    -- ---- Child ----
    C.close(master[0])

    -- Become session leader and give slave PTY as controlling terminal.
    C.setsid()
    C.ioctl(slave[0], C.TIOCSCTTY, 0)

    -- Propagate current window size from our tty to the slave PTY if possible.
    local ws = ffi.new("struct winsize[1]")
    if C.ioctl(0, C.TIOCGWINSZ, ws) == 0 then
      C.ioctl(slave[0], C.TIOCSWINSZ, ws)
    end

    -- stdio -> slave
    C.dup2(slave[0], 0)
    C.dup2(slave[0], 1)
    C.dup2(slave[0], 2)
    C.close(slave[0])

    -- Restore default SIGINT in the child
    local SIG_DFL = ffi.cast("sighandler_t", 0)
    C.signal(C.SIGINT, SIG_DFL)

    -- Exec
    C.execvp(argv[0], argv)
    -- If exec fails:
    os.exit(127)
  end

  -- ---- Parent ----
  C.close(slave[0])

  -- Poll both: our stdin (fd 0) and PTY master.
  local pfd = ffi.new("struct pollfd[2]")
  pfd[0].fd = 0;             pfd[0].events = C.POLLIN
  pfd[1].fd = master[0];     pfd[1].events = C.POLLIN

  local buf = ffi.new("uint8_t[65536]")

  local child_alive = true
  while child_alive do
    -- Block until either stdin or master is readable (or error/hup).
    local pret = C.poll(pfd, 2, -1)
    if pret < 0 then break end

    -- Data from child -> to our stdout
    if bit.band(pfd[1].revents, C.POLLIN + C.POLLERR + C.POLLHUP) ~= 0 then	
      local n = C.read(master[0], buf, 65536)
      if n > 0 then
        C.write(1, buf, n)
      else
        -- EOF/HUP on PTY: child likely exited or closed stdio.
        child_alive = false
      end
    end

    -- Our keyboard -> to child
    if bit.band(pfd[0].revents, C.POLLIN) ~= 0 then
      local n = C.read(0, buf, 65536)
      if n > 0 then
        -- Forward keystrokes to the child's PTY.
        C.write(master[0], buf, n)
      end
    end

    -- Reap without blocking
    local status = ffi.new("int[1]")
    local w = C.waitpid(pid, status, C.WNOHANG)
    if w == pid then child_alive = false end
  end

  -- Final reap (blocking to be safe)
  C.waitpid(pid, nil, 0)

  -- Cleanup and restore our terminal / signals
  C.close(master[0])
  C.tcsetattr(0, C.TCSANOW, orig_term)
  C.signal(C.SIGINT, old_sigint)

  return 0
end

return { simple_exec = simple_exec }



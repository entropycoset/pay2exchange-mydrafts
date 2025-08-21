local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
typedef int pid_t;

int fork(void);
int execvp(const char *file, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);

int openpty(int *amaster, int *aslave, char *name, void *termp, void *winp);
int close(int fd);
int dup2(int oldfd, int newfd);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);

typedef unsigned long fd_mask;
typedef struct {
  fd_mask fds_bits[16];
} fd_set;

void FD_ZERO(fd_set *set);
void FD_SET(int fd, fd_set *set);
int FD_ISSET(int fd, fd_set *set);
]]

local C = ffi.C

-- Helper to convert Lua args to char* array
local function make_argv(args)
    local argv = ffi.new("char *[?]", #args + 1)
    for i, v in ipairs(args) do
        argv[i-1] = ffi.cast("char *", v)
    end
    argv[#args] = nil
    return argv
end

-- Run interactive child in PTY with proper select loop
local function simple_exec(cmd, args, _ignored)
    args = args or {}
    table.insert(args, 1, cmd)
    local argv = make_argv(args)

    local master = ffi.new("int[1]")
    local slave = ffi.new("int[1]")

    if C.openpty(master, slave, nil, nil, nil) ~= 0 then
        error("openpty failed")
    end

    local pid = C.fork()
    if pid < 0 then
        error("fork failed")
    elseif pid == 0 then
        -- child
        C.close(master[0])
        C.dup2(slave[0], 0)
        C.dup2(slave[0], 1)
        C.dup2(slave[0], 2)
        C.close(slave[0])
        C.execvp(cmd, argv)
        ffi.C._exit(127)
    else
        -- parent
        C.close(slave[0])
        local fd_master = master[0]
        local buf = ffi.new("uint8_t[1024]")

        local stdin_fd = 0
        local maxfd = fd_master > stdin_fd and fd_master or stdin_fd
        maxfd = maxfd + 1

        local fd_set_size = ffi.sizeof("fd_set")
        local readfds = ffi.new("fd_set[1]")

        local status = ffi.new("int[1]")
        local child_alive = true

        while child_alive do
            -- setup fd_set
            C.FD_ZERO(readfds)
            C.FD_SET(fd_master, readfds)
            C.FD_SET(stdin_fd, readfds)

            local ret = C.select(maxfd, readfds, nil, nil, nil)
            if ret > 0 then
                -- read from child
                if C.FD_ISSET(fd_master, readfds) ~= 0 then
                    local n = C.read(fd_master, buf, 1024)
                    if n > 0 then
                        io.stdout:write(ffi.string(buf, n))
                        io.stdout:flush()
                    elseif n == 0 then
                        child_alive = false
                    end
                end
                -- read from stdin
                if C.FD_ISSET(stdin_fd, readfds) ~= 0 then
                    local inp = io.read(1)
                    if inp then
                        C.write(fd_master, ffi.cast("const void *", inp), #inp)
                    end
                end
            end
        end

        -- waitpid to get exit code
        C.waitpid(pid, status, 0)
        return bit.rshift(status[0], 8)
    end
end

return { simple_exec = simple_exec }
local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
typedef int pid_t;

int fork(void);
int execvp(const char *file, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);

int openpty(int *amaster, int *aslave, char *name, void *termp, void *winp);
int close(int fd);
int dup2(int oldfd, int newfd);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);

typedef unsigned long fd_mask;
typedef struct {
  fd_mask fds_bits[16];
} fd_set;

void FD_ZERO(fd_set *set);
void FD_SET(int fd, fd_set *set);
int FD_ISSET(int fd, fd_set *set);
]]

local C = ffi.C

-- Helper to convert Lua args to char* array
local function make_argv(args)
    local argv = ffi.new("char *[?]", #args + 1)
    for i, v in ipairs(args) do
        argv[i-1] = ffi.cast("char *", v)
    end
    argv[#args] = nil
    return argv
end

-- Run interactive child in PTY with proper select loop
local function simple_exec(cmd, args, _ignored)
    args = args or {}
    table.insert(args, 1, cmd)
    local argv = make_argv(args)

    local master = ffi.new("int[1]")
    local slave = ffi.new("int[1]")

    if C.openpty(master, slave, nil, nil, nil) ~= 0 then
        error("openpty failed")
    end

    local pid = C.fork()
    if pid < 0 then
        error("fork failed")
    elseif pid == 0 then
        -- child
        C.close(master[0])
        C.dup2(slave[0], 0)
        C.dup2(slave[0], 1)
        C.dup2(slave[0], 2)
        C.close(slave[0])
        C.execvp(cmd, argv)
        ffi.C._exit(127)
    else
        -- parent
        C.close(slave[0])
        local fd_master = master[0]
        local buf = ffi.new("uint8_t[1024]")

        local stdin_fd = 0
        local maxfd = fd_master > stdin_fd and fd_master or stdin_fd
        maxfd = maxfd + 1

        local fd_set_size = ffi.sizeof("fd_set")
        local readfds = ffi.new("fd_set[1]")

        local status = ffi.new("int[1]")
        local child_alive = true

        while child_alive do
            -- setup fd_set
            C.FD_ZERO(readfds)
            C.FD_SET(fd_master, readfds)
            C.FD_SET(stdin_fd, readfds)

            local ret = C.select(maxfd, readfds, nil, nil, nil)
            if ret > 0 then
                -- read from child
                if C.FD_ISSET(fd_master, readfds) ~= 0 then
                    local n = C.read(fd_master, buf, 1024)
                    if n > 0 then
                        io.stdout:write(ffi.string(buf, n))
                        io.stdout:flush()
                    elseif n == 0 then
                        child_alive = false
                    end
                end
                -- read from stdin
                if C.FD_ISSET(stdin_fd, readfds) ~= 0 then
                    local inp = io.read(1)
                    if inp then
                        C.write(fd_master, ffi.cast("const void *", inp), #inp)
                    end
                end
            end
        end

        -- waitpid to get exit code
        C.waitpid(pid, status, 0)
        return bit.rshift(status[0], 8)
    end
end

return { simple_exec = simple_exec }


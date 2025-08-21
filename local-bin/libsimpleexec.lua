local ffi = require("ffi")
local bit = require("bit")

ffi.cdef[[
typedef int pid_t;
typedef void (*sighandler_t)(int);

pid_t fork(void);
int execvp(const char *file, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);
int tcsetpgrp(int fd, pid_t pgrp);
pid_t tcgetpgrp(int fd);
pid_t getpid(void);
int setpgid(pid_t pid, pid_t pgid);
sighandler_t signal(int signum, sighandler_t handler);

static const int SIGINT = 2;
]]

local C = ffi.C

-- Anchor default signal handlers to prevent GC
local SIG_IGN = ffi.cast("sighandler_t", 1)
local SIG_DFL = ffi.cast("sighandler_t", 0)
local anchored_handlers = { SIG_IGN = SIG_IGN, SIG_DFL = SIG_DFL }

-- Convert Lua table args into char* array
local function make_argv(args)
    local argv = ffi.new("char *[?]", #args + 1)
    for i, v in ipairs(args) do
        argv[i-1] = ffi.cast("char *", v)
    end
    argv[#args] = nil
    return argv
end

-- simple_exec(cmd, args, ignored)
local function simple_exec(cmd, args, _ignored)
    args = args or {}
    table.insert(args, 1, cmd)
    local argv = make_argv(args)

    -- save parent terminal foreground
    local parent_fg = 0
    pcall(function() parent_fg = C.tcgetpgrp(0) end)

    local pid = C.fork()
    if pid < 0 then
        error("fork failed")
    elseif pid == 0 then
        -- child
        -- create new process group so it can own terminal
        pcall(function() C.setpgid(0, 0) end)
        -- give terminal foreground to child
        pcall(function() C.tcsetpgrp(0, C.getpid()) end)
        -- restore default SIGINT in child
        C.signal(C.SIGINT, anchored_handlers.SIG_DFL)
        -- execute program
        C.execvp(cmd, argv)
        ffi.C._exit(127) -- exec failed
    else
        -- parent
        -- wait for child
        local status = ffi.new("int[1]")
        C.waitpid(pid, status, 0)
        -- restore terminal to parent
        pcall(function() C.tcsetpgrp(0, parent_fg) end)
        -- exit code
        return bit.rshift(status[0], 8)
    end
end

return { simple_exec = simple_exec }


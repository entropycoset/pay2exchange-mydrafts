local ffi = require("ffi")
local bit = require("bit")

-- Define minimal C functions and types
ffi.cdef[[
typedef int pid_t;

pid_t fork(void);
int execvp(const char *file, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);
int tcsetpgrp(int fd, pid_t pgrp);
pid_t getpid(void);
pid_t getpgrp(void);
int tcgetpgrp(int fd);
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
]]

local C = ffi.C

-- Define SIGINT, SIG_IGN, SIG_DFL manually if not defined
local SIGINT = 2
local SIG_IGN = ffi.cast("sighandler_t", 1) -- typical value
local SIG_DFL = ffi.cast("sighandler_t", 0) -- typical value

-- Anchor signal handlers to prevent GC
local anchored_handlers = {
    SIG_IGN = SIG_IGN,
    SIG_DFL = SIG_DFL
}

-- Helper: convert Lua args to C char* array
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
    pcall(function()
        parent_fg = C.tcgetpgrp(0)
    end)

    local pid = C.fork()
    if pid < 0 then
        error("fork failed")
    elseif pid == 0 then
        -- child process
        -- give child foreground terminal if possible
        pcall(function() C.tcsetpgrp(0, C.getpid()) end)
        -- restore default SIGINT in child
        C.signal(SIGINT, anchored_handlers.SIG_DFL)
        -- execute command
        C.execvp(cmd, argv)
        ffi.C._exit(127) -- exec failed
    else
        -- parent process
        -- ignore SIGINT to prevent Lua script dying
        local old_handler = C.signal(SIGINT, anchored_handlers.SIG_IGN)

        -- optionally set child as foreground (best effort)
        pcall(function() C.tcsetpgrp(0, pid) end)

        -- wait for child to exit
        local status = ffi.new("int[1]")
        C.waitpid(pid, status, 0)

        -- restore terminal foreground
        pcall(function()
            if parent_fg ~= 0 then
                C.tcsetpgrp(0, parent_fg)
            end
        end)

        -- restore original SIGINT handler
        C.signal(SIGINT, old_handler)

        return bit.rshift(status[0], 8)
    end
end

return { simple_exec = simple_exec }


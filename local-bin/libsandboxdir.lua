local lfs = require("lfs")

-- Define the class table
local SandboxDir = {}
SandboxDir.__index = SandboxDir

-- Constructor
function SandboxDir:new(dirname)
    assert(type(dirname) == "string", "Directory name must be a string.")
    assert(dirname:match("^[%w_/-]+$"), "Invalid directory name: only alphanumeric, slash, and underscore allowed.")

    local obj = {
        dirname = dirname,
        original_cwd = lfs.currentdir()
    }
    setmetatable(obj, self)
    return obj
end

-- Create directory, chmod, and enter it
function SandboxDir:start()
    local attr = lfs.attributes(self.dirname)
    if not (attr and attr.mode == "directory") then
        assert(lfs.mkdir(self.dirname), "Failed to create directory.")
        assert(os.execute("chmod 700 " .. self.dirname), "Failed to set permissions.")
        print("Created and chmodded directory:", self.dirname)
    else
        print("Directory already exists:", self.dirname)
    end

    assert(lfs.chdir(self.dirname), "Failed to change working directory.")
    print("Entered directory:", lfs.currentdir())
end

-- Recursively delete directory and restore original CWD
function SandboxDir:finish()
    assert(lfs.chdir(self.original_cwd), "Failed to restore original working directory.")
    print("Restored working directory to:", self.original_cwd)

    local function recursive_delete(path)
				--print("delete: " .. path)
        for file in lfs.dir(path) do
            if file ~= "." and file ~= ".." then
                local fullpath = path .. "/" .. file
                local mode = lfs.attributes(fullpath, "mode")
                if mode == "directory" then
                    recursive_delete(fullpath)
										local deleted = os.remove(fullpath)
										--if not deleted then print("can not remove (dir) " .. fullpath) end
                else
										local deleted = os.remove(fullpath)
										--if not deleted then print("can not remove (file) " .. fullpath) end
                end
            end
        end
        assert(os.remove(path), "Failed to remove root directory: " .. path)
    end

    recursive_delete(self.dirname)
    print("Deleted directory and contents:", self.dirname)
end

-- Return the class as the module
return SandboxDir


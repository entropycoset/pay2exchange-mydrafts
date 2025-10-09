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
				original_cwd = lfs.currentdir(),
				pause = false -- should we pause at end?
		}
		setmetatable(obj, self)
		return obj
end

-- Helper function to create directories recursively
local function mkdir_recursive(path)
		local components = {}
		for component in path:gmatch("[^/]+") do
				table.insert(components, component)
		end

		local current_path = ""
		for i, component in ipairs(components) do
				if i == 1 then
						current_path = component
				else
						current_path = current_path .. "/" .. component
				end

				local attr = lfs.attributes(current_path)
				if not (attr and attr.mode == "directory") then
						local success = lfs.mkdir(current_path)
						if not success then
								error("Sandbox: Failed to create directory: " .. current_path)
						end
						print("Sandbox: Created directory:", current_path)
				end
		end
end

-- Create directory, chmod, and enter it
function SandboxDir:start()
		local attr = lfs.attributes(self.dirname)
		if not (attr and attr.mode == "directory") then
				mkdir_recursive(self.dirname)
				assert(os.execute("chmod 700 " .. self.dirname), "Failed to set permissions.")
				print("Sandbox: Set permissions for directory:", self.dirname)
		else
				print("Sandbox: Directory already exists:", self.dirname)
		end

		assert(lfs.chdir(self.dirname), "Failed to change working directory.")
		print("Sandbox: Entered directory:", lfs.currentdir())
end

function SandboxDir:set_pause(p)
	self.pause = p
end

-- Recursively delete directory and restore original CWD
function SandboxDir:finish()

	if self.pause then
			local filename = "unpause"
			local interval = 0.2
			print("Sandbox: Waiting for " .. filename .. " to appear...")
			while not lfs.attributes(filename) do
				socket.sleep(interval)
			end
			print("Sandbox: Done waiting - unpause")
		end

		print("Sandbox: Ending work in directory " .. self.dirname)
		assert(lfs.chdir(self.original_cwd), "Failed to restore original working directory.")
		print("Sandbox: Restored working directory to:", self.original_cwd)

		local function recursive_delete_contents(path)
				--print("delete: " .. path)
				for file in lfs.dir(path) do
						if file ~= "." and file ~= ".." then
								local fullpath = path .. "/" .. file
								local mode = lfs.attributes(fullpath, "mode")
								if mode == "directory" then
										recursive_delete_contents(fullpath)
									local deleted = os.remove(fullpath)
									--if not deleted then print("can not remove (dir) " .. fullpath) end
								else
									local deleted = os.remove(fullpath)
									--if not deleted then print("can not remove (file) " .. fullpath) end
								end
						end
				end
		end

		-- Only delete the target directory itself, not parent directories
		recursive_delete_contents(self.dirname)
		assert(os.remove(self.dirname), "Failed to remove target directory: " .. self.dirname)
		print("Sandbox: Deleted directory and contents:", self.dirname)
end

-- Return the class as the module
return SandboxDir


-- Copyrighted (C) 2025 EntropyCoset on *GPL* licence (at least for now)
-- various small utils for the programs of p2e (pay2exchange)

function get_runsubdir(arg)
	for i, v in ipairs(arg) do
		local subdir = string.match(v, "^%-runsubdir=(%S+)$")
		if subdir then
			return subdir
		end
	end
	return nil -- no subdir specified
end

function get_userindex(arg)
	for i, v in ipairs(arg) do
		local valmin=0
		local valmax=1000
		local n = string.match(v, "^%-userindex=(%d+)$")
		if n then
			local value = tonumber(n)
			if value >= valmin and value <= valmax then
				return value
			else
				--~ error("Invalid: " .. value .. ". Must be in range " .. valmin .. ".." .. valmax .. ".")
			end
		end
	end
	return -1
end

---------------------------------------------------------------------

function show_args_simple(args)
	print("Program will start with " .. #args .. " argument(s)")
	-- print each argument to verify what we try to execute
	for i, v in ipairs(args) do
		print("ARG[" .. i .. "] = " .. v)
	end
end


function show_args_bin(args)
	print("Arguments for program")
	for _, arg in ipairs(args) do
			print("	 " .. arg)
	end

	if file_is_executable("./showargs") then
			print("-------------------")
			print("Arguments for program, as printed by a dummy program:")
			mylib_simpleexec.simple_exec("./showargs", args, true)
			print("-------------------")
	end -- if binary showargs

end -- our show_args()

function load_p2e_env()
	local ret
	local core_base = os.getenv("P2E_CORE_HOME") -- e.g. /home/user/work/pay2exchange-core/
	print("core_base=[" .. core_base .."]")
	if core_base == nil then
		error("You must set env variable P2E_CORE_HOME")
	end
	if core_base:sub(-1) ~= "/" then
    		core_base = core_base .. "/"
	end
	if core_base == "" then error("P2E_CORE_HOME must be not-empty") end
	return { core_base = core_base }
end -- load_p2e_env

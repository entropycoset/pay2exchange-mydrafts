-- some simple utils; polutes global namespace

---------------------------------------------------------------------
-- also change language objects --

function table.insert2(t, a, b)
	if type(t) ~= "table" then
		error("table.insert2: first argument must be a table", 2)
	end
	table.insert(t, a)
	table.insert(t, b)
	return t
end

---------------------------------------------------------------------

function begins_with(str, prefix)
	if (type(str) ~= "string") or (type(prefix) ~= "string") then error("Must call begin_with() with 2 strings.",2) end
	return str:sub(1, #prefix) == prefix
end

local function dbg_dump_(t, indent)
	indent = indent or "	"
	for k, v in pairs(t) do
		if type(v) == "table" then
			print(indent .. tostring(k) .. ":")
			dbg_dump_(v, indent .. indent)
		else
			print(indent .. tostring(k) .. ": " .. tostring(v))
		end
	end
end

local function dbg_dump(t, indent)
	print("v--(dump)-------------------------")
	dbg_dump_(t,indent)
	print("^--(dump)-------------------------")
end

---------------------------------------------------------------------

local function detect_os()
	local windows_slash = package.config:sub(1,1) == '\\' ;

	if jit and jit.os then
		print("Read jit.os [" .. jit.os .. "]")
		if jit.os == "Windows" then
			if not windows_slash then error("Can not detect OS, conflict between (win) jit.os(" .. jit.os .. ") and slash") end
			return "Windows"
		end
		if jit.os == "OSX" then
			if windows_slash then error("Can not detect OS, conflict between (OSX) jit.os(" .. jit.os .. ") and slash") end
			return "Darwin"
		else -- we assume the rest are unix-like
			if windows_slash then error("Can not detect OS, conflict between (misc) jit.os(" .. jit.os .. ") and slash") end
			return "Unixes"
		end
	else
		print("(Can not read OS name from JIT, will try other detection)")
	end

	if windows_slash then
		return "Windows"
	else
		local uname = io.popen("uname"):read("*l")
		print("Read uname [" .. uname .. "]")
		if uname == "Darwin" then
			return "Darwin"
		else
			return "Unixes"
		end
	end
end

local os_family = detect_os()
print ( "os_family: " .. os_family )

---------------------------------------------------------------------

function extractFilenameFromPath(path)
		if not path then return nil end
		-- Handle both Unix-style (/) and Windows-style (\) path separators
		local filename = path:match("([^/\\]+)$")
		return filename
end

function copy_file(src, dst)
		local cp_result = os.execute("cp " .. src .. " " .. dst)
		if cp_result ~= 0 then
				error("Failed to copy file from '" .. src .. "' to '" .. dst .. "'")
		end
end

local function show_file_all(path)
	path = path or "foo.txt"
	local f, err = io.open(path, "rb")
	if not f then
		print("Error opening file:", err)
		return nil, err
	end

	local content, read_err = f:read("*a")
	f:close()

	if not content then
		print("Error reading file:", read_err or "unknown")
		return nil, read_err or "unknown"
	end

	print(content)
	return true
end

function file_is_executable(path)
	return os.execute('[ -x "' .. path .. '" ]') == 0
end
---------------------------------------------------------------------

function get_arg_int(arg, argname, valmin, valmax, isneeded, valdef)
	for i, v in ipairs(arg) do
		local n = string.match(v, "^%-"..argname.."=(%d+)$")
		if n then
			local port = tonumber(n)
			if port >= valmin and port <= valmax then
				return port
			else
				error("Invalid value of ["..argname.."] = " .. port .. ". Must be in range " .. valmin .. ".." .. valmax)
			end
		end
	end
	if isneeded then
		error("No value was given for argument ["..argname.."]. (range " .. valmin .. ".." .. valmax .. ")")
	else
		return valdef
	end
end

function get_arg_str(arg, argname, isneeded, valdef)
	for i, v in ipairs(arg) do
		local n = string.match(v, "^%-" .. argname .. "=([\32-\126]+)$")
		if n then
			return n
		end
	end
	if isneeded then
		error("No value was given for argument ["..argname.."].")
	else
		return valdef
	end
end

---------------------------------------------------------------------

local json = require("dkjson")	-- requires dkjson (https://github.com/LuaDist/dkjson)

local function read_file(filename)
		local file = io.open(filename, "r")
		if not file then
				error("Failed to open file: " .. filename)
		end
		local content = file:read("*a")
		file:close()
		return content
end

-- Extract keys for the given name
local function extract_keys_jsondata(data, name)
		for _, account in ipairs(data.initial_accounts) do
				if account.name == name then
						local public_key = account.owner_key_full.pub_key
						local private_key = account.owner_key_full.wif_priv_key
						local address = account.owner_key
						return public_key, private_key, address
				end
		end
		return nil, nil, nil
end

function extract_keys_jsonfile(json_fn, target_name)
	if not json_fn then error("extract json keys from file - but no filename") end
	if not target_name then error("extract json keys from file - but no target_name") end
	print("Loading JSON with private genesis keys from (" .. json_fn .. ") for target (" .. target_name .. ")")
		local json_text = read_file(json_fn)
		local data, pos, err = json.decode(json_text, 1, nil)
		if err then
				error("Failed to decode JSON: " .. err)
		end

		local pub, priv, addr = extract_keys_jsondata(data, target_name)

		if pub and priv and addr then
			return { pub = pub, priv = priv, addr = addr }
		else
				print("Can not (fully) load privkeys and data for account (witness): " .. target_name)
		end
end


---------------------------------------------------------------------

-- Required library
local socket = require("socket")

-- Function to check TCP connection
function check_tcp_connection(target_ip, target_port, tcp_timeout, verbose)
	verbose = verbose or 1

		if (verbose>=2) then print("Checking TPC " .. target_ip .. ":" .. target_port .. " for " .. tcp_timeout .. " sec.") ; end
		local tcp = assert(socket.tcp())
		tcp:settimeout(tcp_timeout)

		local success, err = tcp:connect(target_ip, target_port)
		if success then
				local sent, send_err = tcp:send("test")
				tcp:close()
				local ok = sent ~= nil
				if ok then
					if (verbose>=2) then	print("Connection works"); end
					return true
				else
					if (verbose>=1) then io.stderr:write("FAILED TPC (connect, but can not send) " .. target_ip .. ":" .. target_port .. " for " .. tcp_timeout .. " sec.") ; end
				 return false
				end
				return ok
		else
				tcp:close()
				if (verbose>=1) then	print("FAILED TPC (can not connect) " .. target_ip .. ":" .. target_port .. " for " .. tcp_timeout .. " sec.") ; end
				return false
		end
end

function retry_few_times(action, tmax, sleep)
		local tstart = socket.gettime()
		while true do
				local telaps = socket.gettime() - tstart;
				local telaps_str = string.format("%.2f", telaps);
				if telaps > tmax then print("Failed - giving up after " .. telaps_str .. " sec" .. ".") ; return false ; end
				local done = action()
				if done then
					print("Succees after " .. telaps_str .. " sec" .. ".")
					return true
        else
            print("Retrying... (" .. telaps_str .. "/" .. tmax .. " secs" .. ")")
        end
        socket.sleep(sleep)
    end
    exit("dead code")
end

---------------------------------------------------------------------


function both_or_neither(A,B)
	if not (A or B) then return true; end -- good - neither
	if (A and B) then return true; end -- good - both
	return false -- bad
end


---------------------------------------------------------------------


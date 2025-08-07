local json = require("json")
local lib_tablesort = require("lib_tablesort")

local input_file = assert(io.open(arg[1], "r"))
local input_data = input_file:read("*a")
input_file:close()

local lua_obj, pos, err = json.decode(input_data, 1, nil)
if err then error("JSON decode error: " .. err) end

-- Normalize and encode with indent
local final_json = lib_tablesort.encode_sorted(lua_obj, "  ") -- or "\t" for tabs
print(final_json)


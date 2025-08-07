local json = require("dkjson")
local lib_tablesort=require("lib_tablesort")

local input_file = io.open(arg[1], "r")
if not input_file then error("Cannot open input.json for reading") end
local input_data = input_file:read("*a")
input_file:close()

local lua_obj, pos, err = json.decode(input_data, 1, nil)
if err then   error("Error decoding JSON: " .. err) end

-- Use the enhanced encoding that preserves array/object distinction
local final_json = lib_tablesort.encode_sorted(lua_obj, input_data)
print(final_json)


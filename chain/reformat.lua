local json = require("dkjson")

local input_file = io.open(arg[1], "r")
if not input_file then error("Cannot open input.json for reading") end
local input_data = input_file:read("*a")
input_file:close()

local lua_obj, pos, err = json.decode(input_data, 1, nil)
if err then   error("Error decoding JSON: " .. err) end

local output_data = json.encode(lua_obj, { indent = true })

-- Decode JSON to Lua table
local parsed = assert(json.decode(output_data))
-- Canonicalize: re-encode from decoded Lua table
local canonical_json = json.encode(parsed, { indent = true })
-- Output canonicalized JSON
print(canonical_json)


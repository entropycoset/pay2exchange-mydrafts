local json = require("json")  -- Assuming you're using the patched version that preserves object/array distinction

local M = {}

-- Determine if a table is an array (i.e., has sequential numeric keys starting from 1)
local function is_array(t)
  if type(t) ~= "table" then return false end
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then return false end
    count = count + 1
    if t[k] == nil then return false end
  end
  return count == #t
end

-- Recursively normalize the table: sort keys and preserve array/object distinction
local function normalize(value)
  if type(value) ~= "table" then return value end

  if is_array(value) then
    local result = {}
    for i = 1, #value do
      result[i] = normalize(value[i])
    end
    return result
  else
    local keys = {}
    for k in pairs(value) do table.insert(keys, k) end
    table.sort(keys)

    local result = {}
    for _, k in ipairs(keys) do
      result[k] = normalize(value[k])
    end

    -- preserve empty object marker
    if next(result) == nil then
      return setmetatable({}, { __jsontype = "object" })
    end

    return result
  end
end

function M.normalize(input)
  return normalize(input)
end

function M.encode_sorted(input, indent)
  local normalized = normalize(input)
  return json.encode(normalized, indent or "\t" )
end

return M


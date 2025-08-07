local json = require("json")  -- Your patched json.lua

local M = {}

-- Detect if table is a non-empty array (sequential numeric keys starting from 1)
local function is_array(t)
  if type(t) ~= "table" then return false end
  local i = 0
  for k in pairs(t) do
    i = i + 1
    if type(k) ~= "number" or t[i] == nil then return false end
  end
  return i > 0
end

-- Recursively sort keys and tag arrays/objects to preserve JSON identity
local function normalize(value)
  if type(value) ~= "table" then
    return value
  end

  -- Handle array
  if is_array(value) then
    local result = {}
    for i = 1, #value do
      result[i] = normalize(value[i])
    end
    return setmetatable(result, { __jsontype = "array" })
  end

  -- Handle object (key-value map)
  local keys = {}
  for k in pairs(value) do
    table.insert(keys, k)
  end
  table.sort(keys, function(a, b)
    return tostring(a) < tostring(b)
  end)

  local result = {}
  for _, k in ipairs(keys) do
    result[k] = normalize(value[k])
  end

  -- If the object is empty, tag explicitly as object
  if next(result) == nil then
    return setmetatable({}, { __jsontype = "object" })
  end

  return result
end

-- Public API
function M.encode_sorted(data, indent)
  local normalized = normalize(data)
  return json.encode(normalized, indent or "\t")
end

-- If you also want to just normalize (for testing/debugging), export it
M.normalize = normalize

return M


local json = require("dkjson")

local M = {}

-- Determine if a table is an array (i.e. has sequential numeric keys)
local function is_array(t)
  if type(t) ~= "table" then return false end
  local max = 0
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then return false end
    if k > max then max = k end
    count = count + 1
  end
  return count > 0 and count == max
end

-- Recursively normalize table: sort object keys, preserve array type
local function normalize(t)
  if type(t) ~= "table" then return t end

  if is_array(t) then
    local result = {}
    for i = 1, #t do
      result[i] = normalize(t[i])
    end
    return setmetatable(result, { __array = true })  -- Marks this as array for dkjson
  else
    local keys = {}
    for k in pairs(t) do table.insert(keys, k) end
    table.sort(keys, function(a, b)
      return tostring(a) < tostring(b)
    end)

    local result = {}
    for _, k in ipairs(keys) do
      result[k] = normalize(t[k])
    end

    -- Force this to be recognized as object (even if empty)
    return setmetatable(result, { __array = false })  -- Marks this as object
  end
end

-- Main public API
function M.encode_sorted(t)
  local normalized = normalize(t)
  return json.encode(normalized, { indent = true })
end

return M


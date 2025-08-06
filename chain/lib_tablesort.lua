local json = require("dkjson")

local M = {}  -- this is the table we will return

-- Recursively sort all object keys in the table
local function sort_keys(t)
  if type(t) ~= "table" then return t end

  -- Check if this is an array (all keys are numeric and sequential)
  local is_array = true
  local n = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then
      is_array = false
      break
    end
    if k > n then n = k end
  end

  if is_array then
    local result = {}
    for i = 1, n do
      result[i] = sort_keys(t[i])
    end
    return result
  else
    local keys = {}
    for k in pairs(t) do table.insert(keys, k) end
    table.sort(keys)

    local result = {}
    for _, k in ipairs(keys) do
      result[k] = sort_keys(t[k])
    end
    return result
  end
end

-- Public function
function M.encode_sorted(t)
  local sorted = sort_keys(t)
  return json.encode(sorted, { indent = true })
end

return M


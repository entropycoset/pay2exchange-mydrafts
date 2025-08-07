local json = require("json") -- your patched json.lua

local M = {}

-- Check if a table is a JSON array
local function is_array(t)
  if type(t) ~= "table" then return false end
  local max = 0
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then return false end
    if k > max then max = k end
    count = count + 1
  end
  return count == max and count > 0
end

-- Recursively normalize the table:
-- - Sort object keys
-- - Preserve empty array vs object distinction
local function normalize(t)
  if type(t) ~= "table" then return t end

  if is_array(t) then
    local result = {}
    for i = 1, #t do
      result[i] = normalize(t[i])
    end
    return setmetatable(result, { __jsonarray = true }) -- patched json.lua uses this
  else
    local keys = {}
    for k in pairs(t) do table.insert(keys, k) end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)

    local result = {}
    for _, k in ipairs(keys) do
      result[k] = normalize(t[k])
    end

    -- preserve empty object explicitly
    if next(result) == nil then
      return setmetatable({}, { __jsonobject = true })
    end

    return result
  end
end

-- Public API
function M.encode_sorted(tbl, indent)
  local normalized = normalize(tbl)
  return json.encode(normalized, { indent = indent or "\t" }) -- default to tab
end

return M


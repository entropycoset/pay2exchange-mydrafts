local json = require("json")

local M = {}

local function is_array(t)
  if type(t) ~= "table" then return false end
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then return false end
    count = count + 1
  end
  return count > 0 or next(t) == nil  -- Allow empty arrays
end

local function normalize(t)
  if type(t) ~= "table" then return t end

  if is_array(t) then
    local result = {}
    for i = 1, #t do
      result[i] = normalize(t[i])
    end
    return result
  else
    local keys = {}
    for k in pairs(t) do table.insert(keys, k) end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)

    local result = {}
    for _, k in ipairs(keys) do
      result[k] = normalize(t[k])
    end

    -- Ensure empty object is retained
    if next(result) == nil then
      return {}  -- Will be treated as object
    end

    return result
  end
end

function M.encode_sorted(t)
  local normalized = normalize(t)
  return json.encode(normalized)
end

return M


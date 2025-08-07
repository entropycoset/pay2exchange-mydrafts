local json = require("dkjson")

local M = {}

-- Recursively sort keys while preserving array/object types from original structure
local function sort_keys_with_structure(t, original, path)
  if type(t) ~= "table" then return t end

  -- Count elements and determine table structure
  local count = 0
  local has_non_numeric_keys = false
  local max_numeric_key = 0
  
  for k, _ in pairs(t) do
    count = count + 1
    if type(k) ~= "number" then
      has_non_numeric_keys = true
    else
      if k > max_numeric_key then max_numeric_key = k end
    end
  end

  -- Handle empty tables
  if count == 0 then
    -- Check original structure to determine if this should be array or object
    local original_value = original
    if path ~= "" then
      -- Navigate to the corresponding location in original structure
      for part in path:gmatch("[^%.]+") do
        if original_value and type(original_value) == "table" then
          original_value = original_value[part]
        else
          original_value = nil
          break
        end
      end
    end
    
    if original_value and type(original_value) == "table" then
      -- Check if original was an array by looking for numeric keys
      local original_is_array = false
      local original_count = 0
      for k, _ in pairs(original_value) do
        original_count = original_count + 1
        if type(k) == "number" then
          original_is_array = true
        else
          original_is_array = false
          break
        end
      end
      
      if original_count == 0 then
        -- Empty in original - check metatable or use dkjson array detection
        local mt = getmetatable(original_value)
        if mt and mt.__jsontype == 'array' then
          original_is_array = true
        end
      end
      
      if original_is_array then
        local result = {}
        setmetatable(result, {__jsontype = 'array'})
        return result
      end
    end
    
    -- Default to empty object
    return {}
  end

  -- Determine if this should be treated as an array
  local is_array = not has_non_numeric_keys and count > 0
  
  if is_array then
    -- Verify it's a proper sequential array starting from 1
    for i = 1, max_numeric_key do
      if t[i] == nil then
        is_array = false
        break
      end
    end
  end

  if is_array then
    local result = {}
    setmetatable(result, {__jsontype = 'array'}) -- Mark as array
    for i = 1, max_numeric_key do
      local new_path = path == "" and tostring(i) or path .. "." .. tostring(i)
      result[i] = sort_keys_with_structure(t[i], original, new_path)
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
      local new_path = path == "" and tostring(k) or path .. "." .. tostring(k)
      result[k] = sort_keys_with_structure(t[k], original, new_path)
    end

    -- Force this to be recognized as object (even if empty)
    return setmetatable(result, { __array = false })  -- Marks this as object
  end
end

-- Special marker for empty objects
local EMPTY_OBJECT_MARKER = "____EMPTY_OBJECT_MARKER____"

-- Replace empty objects with markers before encoding
local function mark_empty_objects_for_encoding(t)
  if type(t) ~= "table" then return t end
  
  local count = 0
  for _ in pairs(t) do count = count + 1 end
  
  if count == 0 then
    local mt = getmetatable(t)
    if not (mt and mt.__jsontype == 'array') then
      -- This should be an empty object
      return EMPTY_OBJECT_MARKER
    end
    -- This should be an empty array - leave as is
    return t
  end
  
  -- Recursively process non-empty tables
  local result = {}
  for k, v in pairs(t) do
    result[k] = mark_empty_objects_for_encoding(v)
  end
  
  -- Preserve metatable if it exists
  if getmetatable(t) then
    setmetatable(result, getmetatable(t))
  end
  
  return result
end

-- Public function that preserves array/object distinction
function M.encode_sorted(t, original_json)
  local original_structure = nil
  
  -- If we have original JSON string, decode it to get the original structure
  if original_json and type(original_json) == "string" then
    local decoded_original, pos, err = json.decode(original_json)
    if decoded_original and not err then
      original_structure = decoded_original
    end
  end
  
  local sorted = sort_keys_with_structure(t, original_structure, "")
  
  -- Mark empty objects with special marker
  local marked = mark_empty_objects_for_encoding(sorted)
  
  -- Encode with dkjson
  local json_str = json.encode(marked, { indent = true })
  
  -- Replace markers with proper empty objects
  json_str = json_str:gsub('"' .. EMPTY_OBJECT_MARKER .. '"', '{}')
  
  return json_str
end

-- Simpler version for when we don't have original JSON
function M.encode_sorted_simple(t)
  local sorted = sort_keys_with_structure(t, nil, "")
  return json.encode(sorted, { indent = true })
end

return M


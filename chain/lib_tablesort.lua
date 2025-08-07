local json = require("dkjson")

local M = {}

-- Table to track array/object nature of empty containers
local container_types = {}

-- Parse JSON string to identify positions and types of empty containers
local function identify_empty_container_types(json_str)
  container_types = {} -- Reset
  local containers = {}
  local pos = 1
  local container_count = 0
  
  -- Find all empty containers and their types
  while pos <= #json_str do
    local empty_array_start = json_str:find("%[%s*%]", pos)
    local empty_object_start = json_str:find("%{%s*%}", pos)
    
    local next_pos = nil
    local container_type = nil
    
    if empty_array_start and (not empty_object_start or empty_array_start < empty_object_start) then
      container_count = container_count + 1
      containers[container_count] = "array"
      next_pos = empty_array_start + json_str:match("%[%s*%]", empty_array_start):len()
    elseif empty_object_start then
      container_count = container_count + 1
      containers[container_count] = "object"
      next_pos = empty_object_start + json_str:match("%{%s*%}", empty_object_start):len()
    else
      break
    end
    
    pos = next_pos
  end
  
  return containers
end

-- Counter for empty containers encountered during traversal
local empty_container_index = 0

-- Recursively sort all object keys in the table while preserving array/object distinction
local function sort_keys(t, empty_container_map)
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
    empty_container_index = empty_container_index + 1
    local container_type = empty_container_map and empty_container_map[empty_container_index]
    
    if container_type == "array" then
      -- Return empty array
      local result = {}
      setmetatable(result, {__jsontype = 'array'})
      return result
    else
      -- Return empty object (default)
      return {}
    end
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
      result[i] = sort_keys(t[i], empty_container_map)
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
      result[k] = sort_keys(t[k], empty_container_map)
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
  local empty_container_map = nil
  
  -- If we have original JSON string, identify empty container types
  if original_json and type(original_json) == "string" then
    -- First decode normally
    local decoded_obj, pos, err = json.decode(original_json)
    if decoded_obj and not err then
      t = decoded_obj
    end
    
    -- Then identify empty container types from original JSON
    empty_container_map = identify_empty_container_types(original_json)
  end
  
  -- Reset counter for this traversal
  empty_container_index = 0
  local sorted = sort_keys(t, empty_container_map)
  
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
  empty_container_index = 0
  local sorted = sort_keys(t, nil)
  return json.encode(sorted, { indent = true })
end

return M


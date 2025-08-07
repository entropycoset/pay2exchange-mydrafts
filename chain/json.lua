--
-- json.lua
--
-- Copyright (c) 2020 rxi
-- Copyright (c) 2025 EntropyCoset
--	
-- Permission is hereby granted, free of charge, to any person obtaining a copy of
-- this software and associated documentation files (the "Software"), to deal in
-- the Software without restriction, including without limitation the rights to
-- use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
-- of the Software, and to permit persons to whom the Software is furnished to do
-- so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--

-- Based on the original dkjson.lua but adapted for key-ordering and array/object distinction.

--
-- json.lua
-- JSON encoding and decoding in pure Lua.
--

local json = { _version = "0.1.3" }

local encode -- forward declaration
	
-- Determine whether a table is a JSON array (sequence)
local function is_array(t)
  if getmetatable(t) and getmetatable(t).__jsontype == "array" then
    return true
  end
  local max = 0
  local count = 0
  for k, v in pairs(t) do
    if type(k) ~= "number" then return false end
    if k > max then max = k end
    count = count + 1
  end
  return max == count and count > 0
end

-- Escape characters for strings
local escape_char_map = {
  ["\\"] = "\\\\",
  ["\""] = "\\\"",
  ["\b"] = "\\b",
  ["\f"] = "\\f",
  ["\n"] = "\\n",
  ["\r"] = "\\r",
  ["\t"] = "\\t"
}

local function escape_char(c)
  return escape_char_map[c] or string.format("\\u%04x", c:byte())
end

-- Encode strings
local function encode_string(s)
  return '"' .. s:gsub('[%z\1-\31\\"]', escape_char) .. '"'
end

-- Encode tables
local function encode_table(val, indent, level)
  local mt = getmetatable(val)
  local is_arr = is_array(val)
  print( "array? " .. tostring(is_arr) .. "\n" )
  local jsontype = mt and mt.__jsontype

  local tokens = {}
  local spacing = indent and "\n" or ""
  local indent_str = indent and string.rep(indent, level or 0) or ""
  local next_indent = indent and string.rep(indent, (level or 0) + 1) or ""

  if is_arr or jsontype == "array" then
    for i = 1, #val do
      table.insert(tokens, (next_indent or "") .. encode(val[i], indent, (level or 0) + 1))
    end
    if indent then
      return "[" .. spacing .. table.concat(tokens, "," .. spacing) .. spacing .. indent_str .. "]"
    else
      return "[" .. table.concat(tokens, ",") .. "]"
    end
  else
    local keys = {}
    for k in pairs(val) do table.insert(keys, k) end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do
      local key = encode_string(tostring(k))
      local value = encode(val[k], indent, (level or 0) + 1)
      table.insert(tokens, (next_indent or "") .. key .. ":" .. (indent and " " or "") .. value)
    end
    if indent then
      return "{" .. spacing .. table.concat(tokens, "," .. spacing) .. spacing .. indent_str .. "}"
    else
      return "{" .. table.concat(tokens, ",") .. "}"
    end
  end
end

-- Main encode dispatcher
encode = function(val, indent, level)
  local t = type(val)
  if t == "nil" then
    return "null"
  elseif t == "number" or t == "boolean" then
    return tostring(val)
  elseif t == "string" then
    return encode_string(val)
  elseif t == "table" then
    return encode_table(val, indent, level)
  else
    error("Cannot encode type: " .. t)
  end
end

json.encode = function(val, options)
  local indent = nil
  if type(options) == "table" and options.indent then
    indent = options.indent
  elseif type(options) == "string" then
    indent = options
  end
  return encode(val, indent, 0)
end

-- Simple JSON decode implementation
function json.decode(str, pos, nullval)
  pos = pos or 1

  local function skip_whitespace()
    while str:sub(pos, pos):match("%s") do pos = pos + 1 end
  end

  local function parse_null()
    if str:sub(pos, pos + 3) == "null" then
      pos = pos + 4
      return nullval
    end
  end

  local function parse_boolean()
    if str:sub(pos, pos + 3) == "true" then
      pos = pos + 4
      return true
    elseif str:sub(pos, pos + 4) == "false" then
      pos = pos + 5
      return false
    end
  end

  local function parse_number()
    local start = pos
    while str:sub(pos, pos):match("[%d%.%+%-%eE]") do pos = pos + 1 end
    local num = tonumber(str:sub(start, pos - 1))
    return num
  end

  local function parse_string()
    pos = pos + 1
    local start = pos
    local result = ""
    while pos <= #str do
      local c = str:sub(pos, pos)
      if c == "\"" then
        result = result .. str:sub(start, pos - 1)
        pos = pos + 1
        return result
      elseif c == "\\" then
        result = result .. str:sub(start, pos - 1)
        local esc = str:sub(pos + 1, pos + 1)
        local map = { b = "\b", f = "\f", n = "\n", r = "\r", t = "\t", ["\\"] = "\\", ['"'] = '"', ["/"] = "/" }
        if map[esc] then
          result = result .. map[esc]
          pos = pos + 2
        elseif esc == "u" then
          local hex = str:sub(pos + 2, pos + 5)
          result = result .. utf8.char(tonumber(hex, 16))
          pos = pos + 6
        end
        start = pos
      else
        pos = pos + 1
      end
    end
  end

  local function parse_array()
    pos = pos + 1
    local result = {}
    skip_whitespace()
    if str:sub(pos, pos) == "]" then
      pos = pos + 1
      return setmetatable({}, { __jsontype = "array" })
    end
    while true do
      table.insert(result, parse_value())
      skip_whitespace()
      if str:sub(pos, pos) == "]" then
        pos = pos + 1
        return setmetatable(result, { __jsontype = "array" })
      end
      pos = pos + 1
    end
  end

  local function parse_object()
    pos = pos + 1
    local result = {}
    skip_whitespace()
    if str:sub(pos, pos) == "}" then
      pos = pos + 1
      return result
    end
    while true do
      skip_whitespace()
      local key = parse_string()
      skip_whitespace()
      pos = pos + 1
      skip_whitespace()
      result[key] = parse_value()
      skip_whitespace()
      if str:sub(pos, pos) == "}" then
        pos = pos + 1
        return result
      end
      pos = pos + 1
    end
  end

  function parse_value()
    skip_whitespace()
    local c = str:sub(pos, pos)
    if c == "\"" then return parse_string()
    elseif c == "{" then return parse_object()
    elseif c == "[" then return parse_array()
    elseif c == "n" then return parse_null()
    elseif c == "t" or c == "f" then return parse_boolean()
    else return parse_number()
    end
  end

  return parse_value()
end

return json


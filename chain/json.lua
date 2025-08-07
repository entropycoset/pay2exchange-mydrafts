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

------------------------------------------------------------------------
-- Encode
------------------------------------------------------------------------

local escape_char_map = {
  [ "\\" ] = "\\\\",
  [ "\"" ] = "\\\"",
  [ "\b" ] = "\\b",
  [ "\f" ] = "\\f",
  [ "\n" ] = "\\n",
  [ "\r" ] = "\\r",
  [ "\t" ] = "\\t",
}
local escape_char_map_inv = { ["\\/"] = "/" }
for k, v in pairs(escape_char_map) do
  escape_char_map_inv[v] = k
end

local function escape_char(c)
  return escape_char_map[c] or string.format("\\u%04x", c:byte())
end

local function encode_string(s)
  return '"' .. s:gsub('[%z\1-\31\\"]', escape_char) .. '"'
end

local function is_array(t)
  if type(t) ~= "table" then return false end
  local max = 0
  local count = 0
  for k, _ in pairs(t) do
    if type(k) ~= "number" then return false end
    if k > max then max = k end
    count = count + 1
  end
  return count == max and max > 0
end

local function encode_table(val, indent, level)
  local mt = getmetatable(val)
  local is_obj = mt and mt.__json_object
  local is_arr = mt and mt.__json_array
  local t_is_array = is_arr or (not is_obj and is_array(val))
  local tokens = {}
  local ind, next_indent = "", ""
  if indent and indent ~= true then
    ind = string.rep(indent, level or 0)
    next_indent = string.rep(indent, (level or 0) + 1)
  end

  if t_is_array then
    for i = 1, #val do
      table.insert(tokens,
        (indent and next_indent or "") ..
        json.encode(val[i], indent, (level or 0) + 1)
      )
    end
    if indent then
      return "[\n" .. table.concat(tokens, ",\n") .. "\n" .. ind .. "]"
    else
      return "[" .. table.concat(tokens, ",") .. "]"
    end
  else
    local keys = {}
    for k in pairs(val) do table.insert(keys, k) end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do
      local v = val[k]
      table.insert(tokens,
        (indent and next_indent or "") ..
        encode_string(k) .. ":" .. (indent and " " or "") ..
        json.encode(v, indent, (level or 0) + 1)
      )
    end
    if indent then
      return "{\n" .. table.concat(tokens, ",\n") .. "\n" .. ind .. "}"
    else
      return "{" .. table.concat(tokens, ",") .. "}"
    end
  end
end

function json.encode(val, indent, level)
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
    error("json.encode: unsupported type " .. t)
  end
end

------------------------------------------------------------------------
-- Decode (from original dkjson)
------------------------------------------------------------------------

local function decode_error(str, idx, msg)
  error(string.format("Error at position %d: %s", idx, msg))
end

local function skip_whitespace(str, idx)
  local _, e = str:find("^[ \n\r\t]+", idx)
  if e then return e + 1 else return idx end
end

local function parse_null(str, idx)
  if str:sub(idx, idx + 3) == "null" then
    return nil, idx + 4
  else
    decode_error(str, idx, "expected 'null'")
  end
end

local function parse_true(str, idx)
  if str:sub(idx, idx + 3) == "true" then
    return true, idx + 4
  else
    decode_error(str, idx, "expected 'true'")
  end
end

local function parse_false(str, idx)
  if str:sub(idx, idx + 4) == "false" then
    return false, idx + 5
  else
    decode_error(str, idx, "expected 'false'")
  end
end

local function parse_number(str, idx)
  local num_str = str:match("^-?%d+%.?%d*[eE]?[+-]?%d*", idx)
  if not num_str then
    decode_error(str, idx, "invalid number")
  end
  return tonumber(num_str), idx + #num_str
end

local function parse_string(str, idx)
  local res = ""
  idx = idx + 1 -- skip opening quote
  while idx <= #str do
    local c = str:sub(idx, idx)
    if c == '"' then
      return res, idx + 1
    elseif c == "\\" then
      local next_char = str:sub(idx + 1, idx + 1)
      local esc = escape_char_map_inv["\\" .. next_char]
      if esc then
        res = res .. esc
        idx = idx + 2
      elseif next_char == "u" then
        local hex = str:sub(idx + 2, idx + 5)
        if not hex:match("%x%x%x%x") then
          decode_error(str, idx, "invalid unicode escape")
        end
        res = res .. utf8.char(tonumber(hex, 16))
        idx = idx + 6
      else
        decode_error(str, idx, "invalid escape character")
      end
    else
      res = res .. c
      idx = idx + 1
    end
  end
  decode_error(str, idx, "unclosed string")
end

local function parse_array(str, idx)
  idx = idx + 1
  local res = {}
  local mt = { __json_array = true }
  setmetatable(res, mt)
  idx = skip_whitespace(str, idx)
  if str:sub(idx, idx) == "]" then return res, idx + 1 end
  while true do
    local val
    val, idx = json.decode(str, idx)
    table.insert(res, val)
    idx = skip_whitespace(str, idx)
    local c = str:sub(idx, idx)
    if c == "]" then return res, idx + 1 end
    if c ~= "," then decode_error(str, idx, "expected ',' or ']'") end
    idx = skip_whitespace(str, idx + 1)
  end
end

local function parse_object(str, idx)
  idx = idx + 1
  local res = {}
  local mt = { __json_object = true }
  setmetatable(res, mt)
  idx = skip_whitespace(str, idx)
  if str:sub(idx, idx) == "}" then return res, idx + 1 end
  while true do
    local key
    if str:sub(idx, idx) ~= '"' then
      decode_error(str, idx, "expected string for object key")
    end
    key, idx = parse_string(str, idx)
    idx = skip_whitespace(str, idx)
    if str:sub(idx, idx) ~= ":" then
      decode_error(str, idx, "expected ':' after object key")
    end
    idx = skip_whitespace(str, idx + 1)
    local val
    val, idx = json.decode(str, idx)
    res[key] = val
    idx = skip_whitespace(str, idx)
    local c = str:sub(idx, idx)
    if c == "}" then return res, idx + 1 end
    if c ~= "," then decode_error(str, idx, "expected ',' or '}'") end
    idx = skip_whitespace(str, idx + 1)
  end
end

function json.decode(str, idx)
  idx = skip_whitespace(str, idx or 1)
  local c = str:sub(idx, idx)
  if c == "{" then return parse_object(str, idx)
  elseif c == "[" then return parse_array(str, idx)
  elseif c == '"' then return parse_string(str, idx)
  elseif c == "n" then return parse_null(str, idx)
  elseif c == "t" then return parse_true(str, idx)
  elseif c == "f" then return parse_false(str, idx)
  else return parse_number(str, idx) end
end

return json


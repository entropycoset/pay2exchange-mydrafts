--
-- json.lua
--
-- Copyright (c) 2020 rxi
-- Copyright (c) 2025 changes by chaoscoset
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

local json = { _version = "0.1.2" }

-------------------------------------------------------------------------------
-- Encode
-------------------------------------------------------------------------------

local encode

local escape_char_map = {
  [ "\\" ] = "\\\\",
  [ "\"" ] = "\\\"",
  [ "\b" ] = "\\b",
  [ "\f" ] = "\\f",
  [ "\n" ] = "\\n",
  [ "\r" ] = "\\r",
  [ "\t" ] = "\\t",
}

local function escape_char(c)
  return escape_char_map[c] or string.format("\\u%04x", c:byte())
end

local function encode_nil()
  return "null"
end

local function encode_table(val, indent, level)
  local is_array = (#val > 0)
  local next_indent = indent and (indent ~= true) and (string.rep(indent, level + 1)) or ""
  local this_indent = indent and (indent ~= true) and (string.rep(indent, level)) or ""

  local res = {}
  if is_array then
    for i = 1, #val do
      local v = encode(val[i], indent, level + 1)
      table.insert(res, indent and ("\n" .. next_indent .. v) or v)
    end
    return "[" .. table.concat(res, "," ) .. (indent and ("\n" .. this_indent) or "") .. "]"
  else
    for k, v in pairs(val) do
      local key = encode(k, indent, level + 1)
      local value = encode(v, indent, level + 1)
      local pair = indent and ("\n" .. next_indent .. key .. ": " .. value) or (key .. ":" .. value)
      table.insert(res, pair)
    end
    return "{" .. table.concat(res, "," ) .. (indent and ("\n" .. this_indent) or "") .. "}"
  end
end

local function encode_string(val)
  return '"' .. val:gsub('[%z\1-\31\\"]', escape_char) .. '"'
end

local function encode_number(val)
  -- Check for NaN, inf
  if val ~= val or val == math.huge or val == -math.huge then
    error("unexpected number value '" .. tostring(val) .. "'")
  end
  return tostring(val)
end

local type_func_map = {
  ["nil"] = encode_nil,
  ["table"] = encode_table,
  ["string"] = encode_string,
  ["number"] = encode_number,
  ["boolean"] = tostring,
}

encode = function(val, indent, level)
  level = level or 0
  local t = type(val)
  local f = type_func_map[t]
  if f then
    return f(val, indent, level)
  else
    error("unexpected type '" .. t .. "'")
  end
end

function json.encode(val, opts)
  local indent = (opts and opts.indent) and (opts.indent == true and "  " or opts.indent)
  return encode(val, indent, 0)
end

-------------------------------------------------------------------------------
-- Decode (unchanged from original)
-------------------------------------------------------------------------------

local parse

local function create_set(...)
  local res = {}
  for i = 1, select("#", ...) do res[ select(i, ...) ] = true end
  return res
end

local space_chars   = create_set(" ", "\t", "\r", "\n")
local delim_chars   = create_set(" ", "\t", "\r", "\n", "]", "}", ",")
local escape_chars  = create_set("\\", "/", "\"", "b", "f", "n", "r", "t", "u")
local literals      = create_set("true", "false", "null")

local literal_map = {
  ["true"] = true,
  ["false"] = false,
  ["null"] = nil,
}

local function next_char(str, idx, set, negate)
  for i = idx, #str do
    if set[str:sub(i, i)] ~= negate then
      return i
    end
  end
  return #str + 1
end

local function decode_error(str, idx, msg)
  local line_count = 1
  local col_count = 1
  for i = 1, idx - 1 do
    col_count = col_count + 1
    if str:sub(i, i) == "\n" then
      line_count = line_count + 1
      col_count = 1
    end
  end
  error(string.format("%s at line %d col %d", msg, line_count, col_count))
end

local function codepoint_to_utf8(n)
  -- http://scripts.sil.org/cms/scripts/page.php?site_id=nrsi&id=iws-appendixa
  local f = math.floor
  if n <= 0x7f then
    return string.char(n)
  elseif n <= 0x7ff then
    return string.char(f(n / 64) + 192, n % 64 + 128)
  elseif n <= 0xffff then
    return string.char(f(n / 4096) + 224, f(n % 4096 / 64) + 128, n % 64 + 128)
  elseif n <= 0x10ffff then
    return string.char(f(n / 262144) + 240, f(n % 262144 / 4096) + 128,
                       f(n % 4096 / 64) + 128, n % 64 + 128)
  end
  error(string.format("invalid unicode codepoint '%x'", n))
end

local function parse_unicode_escape(s)
  local n1 = tonumber(s:sub(1, 4), 16)
  local n2 = tonumber(s:sub(7, 10), 16)
  if n2 then
    return codepoint_to_utf8((n1 - 0xd800) * 0x400 + (n2 - 0xdc00) + 0x10000)
  else
    return codepoint_to_utf8(n1)
  end
end

local function parse_string(str, i)
  local res = ""
  local j = i + 1
  local k = j

  while j <= #str do
    local c = str:sub(j, j)

    if c == "\"" then
      res = res .. str:sub(k, j - 1)
      return res, j + 1

    elseif c == "\\" then
      res = res .. str:sub(k, j - 1)
      local esc = str:sub(j + 1, j + 1)
      if not escape_chars[esc] then
        decode_error(str, j, "invalid escape char '" .. esc .. "'")
      end
      if esc == "u" then
        local hex = str:match("^u(%x%x%x%x)", j + 2)
        if not hex then
          decode_error(str, j, "invalid unicode escape")
        end
        res = res .. parse_unicode_escape(hex)
        j = j + 6
      else
        local map = {
          b = "\b", f = "\f", n = "\n", r = "\r", t = "\t",
          ["/"] = "/", ["\\"] = "\\", ['"'] = '"'
        }
        res = res .. map[esc]
        j = j + 2
      end
      k = j
    else
      j = j + 1
    end
  end

  decode_error(str, i, "expected closing quote for string")
end

local function parse_number(str, i)
  local x = i
  local s = str:match("^%-?%d+%.?%d*[eE]?[+%-]?%d*", i)
  if not s then
    decode_error(str, i, "invalid number")
  end
  local num = tonumber(s)
  if not num then
    decode_error(str, i, "invalid number conversion")
  end
  return num, i + #s
end

local function parse_literal(str, i)
  for lit in pairs(literals) do
    if str:sub(i, i + #lit - 1) == lit then
      return literal_map[lit], i + #lit
    end
  end
  decode_error(str, i, "invalid literal")
end

local function parse_array(str, i)
  local res = {}
  i = next_char(str, i + 1, space_chars, true)
  if str:sub(i, i) == "]" then return res, i + 1 end
  local val
  while true do
    val, i = parse(str, i)
    table.insert(res, val)
    i = next_char(str, i, space_chars, true)
    local c = str:sub(i, i)
    i = i + 1
    if c == "]" then break
    elseif c ~= "," then decode_error(str, i, "expected ',' or ']'") end
  end
  return res, i
end

local function parse_object(str, i)
  local res = {}
  i = next_char(str, i + 1, space_chars, true)
  if str:sub(i, i) == "}" then return res, i + 1 end
  while true do
    local key, val
    if str:sub(i, i) ~= "\"" then
      decode_error(str, i, "expected string for key")
    end
    key, i = parse(str, i)
    i = next_char(str, i, space_chars, true)
    if str:sub(i, i) ~= ":" then
      decode_error(str, i, "expected ':' after key")
    end
    i = next_char(str, i + 1, space_chars, true)
    val, i = parse(str, i)
    res[key] = val
    i = next_char(str, i, space_chars, true)
    local c = str:sub(i, i)
    i = i + 1
    if c == "}" then break
    elseif c ~= "," then decode_error(str, i, "expected ',' or '}'") end
  end
  return res, i
end

parse = function(str, idx)
  idx = next_char(str, idx, space_chars, true)
  local c = str:sub(idx, idx)

  if c == "{" then
    return parse_object(str, idx)
  elseif c == "[" then
    return parse_array(str, idx)
  elseif c == "\"" then
    return parse_string(str, idx)
  elseif c:match("[%+%-%.%d]") then
    return parse_number(str, idx)
  else
    return parse_literal(str, idx)
  end
end

function json.decode(str)
  local res, idx = parse(str, 1)
  idx = next_char(str, idx, space_chars, true)
  if idx <= #str then
    decode_error(str, idx, "trailing garbage")
  end
  return res
end

return json


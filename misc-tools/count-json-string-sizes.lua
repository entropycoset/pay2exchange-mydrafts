#!/usr/bin/env lua
-- Copyright: this is vibecoded - none/publicdomain

-- require the system-installed JSON library
local cjson = require "cjson"

local cfg_strlen_min = 10
local cfg_strlen_max = 300

-- Recursively collect all string values
local function extract_strings(obj, out)
    local t = type(obj)
    if t == "string" then
        out[#out+1] = obj
    elseif t == "table" then
        for _, v in pairs(obj) do
            extract_strings(v, out)
        end
    end
end

-- Read entire file into a string
local function slurp(path)
    local f = assert(io.open(path, "r"), "Could not open file: "..tostring(path))
    local content = f:read("*a")
    f:close()
    return content
end

-- Main entry
local args = {...}
if #args ~= 1 then
    io.stderr:write(string.format("Usage: %s <file.json>\n", arg[0]))
    os.exit(1)
end

local text = slurp(args[1])
local ok, data = pcall(cjson.decode, text)
if not ok then
    io.stderr:write("Failed to parse JSON\n")
    os.exit(1)
end

-- 1. Extract and print all strings
local strings = {}
extract_strings(data, strings)

print("All strings found in the JSON:")
print("--------------------------------")
for _, s in ipairs(strings) do
	if #s > cfg_strlen_min and #s < cfg_strlen_max then
	    print(s)
	end
end

-- 2. Build histogram of string lengths
local hist = {}
local example = {}
for _, s in ipairs(strings) do
    local len = #s
    if len > cfg_strlen_min and len < cfg_strlen_max then
        hist[len] = (hist[len] or 0) + 1  -- <==== increments histogram count
        example[len] = s
    end
end

local biggest_count=0
for l,c in pairs(hist) do biggest_count = math.max(biggest_count, c)  end
print(string.format("max count %d", biggest_count))
		
-- 3. Display text histogram
print("\nHistogram of string lengths:")
print("----------------------------")
-- collect and sort lengths
local lengths = {}
for l in pairs(hist) do lengths[#lengths+1] = l end
table.sort(lengths)
for _, l in ipairs(lengths) do
    local count = hist[l]
    local count_frac_of_biggest = ( count / (math.max(1,biggest_count)) )
    local bar_len_max = 50
    local bar_len = math.ceil(bar_len_max*count_frac_of_biggest)
    --if count_frac_of_biggest > 0.01 then
	    local bar = string.rep("#", bar_len) .. string.rep(" ", bar_len_max - bar_len)
	    print(string.format("%3d | %s (%d) - e.g. %s", l, bar, count, example[l]))
    --end
end

local counts={}
for ix, l in ipairs(lengths) do
	print(string.format("ix=%d has l=%d and hist[l]=%d", ix, l, hist[l]));
	-- counts[l] = hist[l]
end
table.sort(counts)

local iii=0
for l, c in ipairs(counts) do
	iii = iii + 1
	if iii > 10 then break end
	print(string.format("len=%d has count=%d", l, c));
end

print(string.format("max count %d", biggest_count))



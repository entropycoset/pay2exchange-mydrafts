-- colordetect.lua
-- Simple library to detect 8/16/256-ish color support and to wrap text accordingly.

local M = {}

-- library-local state
local anycolor = false
local fg16 = false
local bg16 = false
local full16 = false
local colors_count = nil

-- helpers to run shell commands
local function run(cmd)
  local f = io.popen(cmd)
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

local function get_tput_colors()
  local out = run("tput colors 2>/dev/null")
  if not out or out == "" then return nil end
  local n = tonumber(out:match("%d+"))
  return n
end

-- inspect infocmp heuristically for setab/setaf forms
local function infocmp_suggests_bright_bg(term)
  if not term or term == "" then return nil end
  local out = run(("infocmp %s 2>/dev/null"):format(term))
  if not out or out == "" then return nil end

  -- try to find setab capability token
  local setab = out:match("setab=([^,]*)")
  if setab then
    -- look for patterns that suggest 100-107 style or parameter that yields 10x
    if setab:find("10%%p") or setab:find("100") or setab:find("1%%p") then
      return true
    end
    -- look for 4%p1 style mapping (40-47)
    if setab:find("4%%p1") or setab:find("\\E%[4%%p1") then
      return false
    end
  end

  -- fallback: check reported colors in the entry
  local colors = out:match("colors#=(%d+)")
  if colors then
    local n = tonumber(colors)
    if n and n >= 256 then return true end
    if n and n < 16 then return false end
  end

  return nil
end

-- Public: detect_colors()
-- Populates library-local variables: anycolor, fg16, bg16, colors_count
-- Detection rules implemented:
--  - set anycolor = true if tput reports >=8 colors
--  - set fg16 = true if tput reports >=16
--  - set bg16 = true if tput reports >=256 OR infocmp indicates bright-bg capability when tput==16
--  - if inconclusive, bg16 stays false
function M.detect_colors()
  local term = os.getenv("TERM") or ""
  local n = get_tput_colors()
  colors_count = n

  if n then
    anycolor = n >= 8
    fg16 = n >= 16
    if n >= 256 then
      bg16 = true
    elseif n and n < 16 then
      bg16 = false
    else
      -- n == 16 => ambiguous for bright backgrounds, try infocmp
      local ic = infocmp_suggests_bright_bg(term)
      if ic == true then
        bg16 = true
      elseif ic == false then
        bg16 = false
      else
        bg16 = false -- per your requirement: if not sure or can't test then assume NO
      end
    end
  else
    -- tput not available: conservative defaults
    anycolor = false
    fg16 = false
    bg16 = false
  end
  full16 = anycolor and fg16 and bg16

  -- return a summary table
  return {
    anycolor = anycolor,
    fg16 = fg16,
    bg16 = bg16,
    colors = colors_count,
    full16 = full16,
  }
end

-- internal: map a numeric color index (0-15) to SGR sequence for fg or bg
-- expects numeric color value where 0-7 = dark, 8-15 = bright
local function fg_code_for(idx)
  idx = tonumber(idx) or 0
  if idx < 0 then idx = 0 end
  if idx <= 7 then
    return tostring(30 + idx)
  else
    return tostring(90 + (idx - 8))
  end
end

local function bg_code_for(idx)
  idx = tonumber(idx) or 0
  if idx < 0 then idx = 0 end
  if idx <= 7 then
    return tostring(40 + idx)
  else
    return tostring(100 + (idx - 8))
  end
end

-- clamp helpers according to detected capabilities
local function clamp_fg(idx)
  idx = tonumber(idx) or 0
  if idx >= 8 and not fg16 then
    -- map bright down to dark equivalent
    return idx - 8
  end
  if idx < 0 then return 0 end
  if idx > 15 then return 15 end
  return idx
end

local function clamp_bg(idx)
  idx = tonumber(idx) or 0
  if idx >= 8 and not bg16 then
    return idx - 8
  end
  if idx < 0 then return 0 end
  if idx > 15 then return 15 end
  return idx
end

-- Public: colortxt(text, fg, bg)
-- fg and bg are numeric color indexes 0..15 (0=black ... 7=white, 8..15 bright)
-- If detect_colors wasn't called, the function will call it once.
-- If anycolor == false then returns text unchanged.
-- Returns string wrapped with ANSI sequences and resets fg/bg to defaults afterwards.
function M.colortxt(txt, fg, bg)
  if colors_count == nil then
    M.detect_colors()
  end

  if not anycolor then
    return txt
  end

  local seq_parts = {}

  if fg ~= nil then
    local fidx = clamp_fg(fg)
    table.insert(seq_parts, fg_code_for(fidx))
  end

  if bg ~= nil then
    local bidx = clamp_bg(bg)
    table.insert(seq_parts, bg_code_for(bidx))
  end

  if #seq_parts == 0 then
    return txt
  end

  local set_seq = "\27[" .. table.concat(seq_parts, ";") .. "m"
  -- Reset only foreground/background to defaults after text (39 resets fg, 49 resets bg)
  local reset_seq = "\27[39;49m"
  return set_seq .. txt .. reset_seq
end

-- expose state for callers who want to inspect
function M.get_state()
  if colors_count == nil then M.detect_colors() end
  return {
    anycolor = anycolor,
    fg16 = fg16,
    bg16 = bg16,
    colors = colors_count,
    full16 = full16,
  }
end

return M


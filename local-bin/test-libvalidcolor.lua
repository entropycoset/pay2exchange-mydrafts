local color = require("libvalidcolor")

color.detect_colors()
local st = color.get_state()
local full16 = st.full16

if full16 then
  print(color.colortxt("full", 2, 0))
else
  print(color.colortxt("limited", 10, 0))
end

if full16 then
  -- black (0) on dark-blue (4)
  print(color.colortxt("full", 0, 4))
else
  -- bright cyan (14) on black (0)
  print(color.colortxt("limited", 14, 0))
end

print(("detected: anycolor=%s fg16=%s bg16=%s colors=%s")
  :format(tostring(st.anycolor), tostring(st.fg16), tostring(st.bg16), tostring(st.colors)))


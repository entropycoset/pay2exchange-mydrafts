-- Standard 16 ANSI color codes (0–7 normal, 8–15 bright)
local color_names = {
    "Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White",
    "Bright Black", "Bright Red", "Bright Green", "Bright Yellow",
    "Bright Blue", "Bright Magenta", "Bright Cyan", "Bright White"
}

for fg = 0, 15 do
    for bg = 0, 15 do
        local fg_code = (fg < 8) and (30 + fg) or (90 + fg - 8)
        local bg_code = (bg < 8) and (40 + bg) or (100 + bg - 8)
        local text = string.format(" FG:%-2d %-13s BG:%-2d %-13s ", fg, color_names[fg+1], bg, color_names[bg+1])
        print(string.format("\27[%d;%dm%s\27[0m", fg_code, bg_code, text))
    end
end


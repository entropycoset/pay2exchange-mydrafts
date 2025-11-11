#ifndef COLORDETECT_H
#define COLORDETECT_H

#include <string>

namespace colordetect {

enum class ColorLevel {
    NONE = 1,   // no color
    BASIC8 = 8, // 8 colors
    FULL16 = 16 // 16 colors (bright variants)
};

struct State {
    ColorLevel fg;
    ColorLevel bg;
    int colors;   // raw number reported by terminfo
    bool anycolor;
    bool full16;
};

// Detect terminal color capabilities
State detect_colors();

// Wrap text with ANSI escape codes for fg/bg (0–15 indices)
std::string colortxt(const std::string& txt, int fg = -1, int bg = -1);

// Expose cached state
State get_state();

} // namespace colordetect

#endif // COLORDETECT_H


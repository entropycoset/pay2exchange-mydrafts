#ifndef COLORDETECT_H
#define COLORDETECT_H

#include <string>

namespace colordetect {

enum class ColorLevel {
		NONE = 1,		// no color
		BASIC8 = 8, // 8 colors
		FULL16 = 16 // 16 colors (bright variants)
};

// Standard ANSI color enums
enum class Color : int {
		// Basic colors (0-7)
		Black = 0,
		Red = 1,
		Green = 2,
		Yellow = 3,
		Blue = 4,
		Magenta = 5,
		Cyan = 6,
		White = 7,

		// Light/Bright colors (8-15)
		LightBlack = 8,		 // Dark gray
		LightRed = 9,			 // Bright red
		LightGreen = 10,	 // Bright green
		LightYellow = 11,  // Bright yellow
		LightBlue = 12,		 // Bright blue
		LightMagenta = 13, // Bright magenta
		LightCyan = 14,		 // Bright cyan
		LightWhite = 15,	 // Bright white

		// Special values
		Default = -1,			 // Use terminal default
		Reset = -2,				 // Reset to terminal default
		Normal = -1				 // Alias for Default
};

struct State {
		ColorLevel fg;
		ColorLevel bg;
		int colors;		// raw number reported by terminfo
		bool anycolor;
		bool full16;
};

// Detect terminal color capabilities
State detect_colors();

// Wrap text with ANSI escape codes for fg/bg (0–15 indices)
std::string colortxt(const std::string& txt, int fg = -1, int bg = -1);

// Enhanced color function using enum values
std::string colortxt(const std::string& txt, Color fg = Color::Default, Color bg = Color::Default);

// String-returning function for operator<< usage
std::string colorstr(const std::string& txt, Color fg = Color::Default, Color bg = Color::Default);

// Expose cached state
State get_state();

} // namespace colordetect

#endif // COLORDETECT_H


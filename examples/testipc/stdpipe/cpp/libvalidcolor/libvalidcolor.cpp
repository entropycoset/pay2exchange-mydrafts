#include "libvalidcolor.hpp"
#include <curses.h>
#include <term.h>
#include <unistd.h>
#include <sstream>

namespace {
		colordetect::ColorLevel fgLevel = colordetect::ColorLevel::NONE;
		colordetect::ColorLevel bgLevel = colordetect::ColorLevel::NONE;
		int colors_count = -1;

		// Map fg index to ANSI code
		std::string fg_code_for(int idx) {
				if (idx < 0) idx = 0;
				if (idx <= 7) return std::to_string(30 + idx);
				return std::to_string(90 + (idx - 8));
		}

		// Map bg index to ANSI code
		std::string bg_code_for(int idx) {
				if (idx < 0) idx = 0;
				if (idx <= 7) return std::to_string(40 + idx);
				return std::to_string(100 + (idx - 8));
		}

		int clamp_fg(int idx) {
				if (idx < 0) return 0;
				if (idx > 15) idx = 15;
				if (idx >= 8 && fgLevel != colordetect::ColorLevel::FULL16) return idx - 8;
				return idx;
		}

		int clamp_bg(int idx) {
				if (idx < 0) return 0;
				if (idx > 15) idx = 15;
				if (idx >= 8 && bgLevel != colordetect::ColorLevel::FULL16) return idx - 8;
				return idx;
		}

		bool seq_supports_bright(const char* cap) {
				if (!cap || cap == (char*)-1) return false;
				std::string s(cap);
				// Heuristic: look for %p1 with offset 8 or codes >=90/100
				if (s.find("90") != std::string::npos || s.find("100") != std::string::npos)
						return true;
				if (s.find("%p1%{8}") != std::string::npos)
						return true;
				return false;
		}
}

namespace colordetect {

State detect_colors() {
		int err;
		if (setupterm(nullptr, fileno(stdout), &err) != OK) {
				fgLevel = ColorLevel::NONE;
				bgLevel = ColorLevel::NONE;
				colors_count = -1;
				return {fgLevel, bgLevel, colors_count, false, false};
		}

		colors_count = tigetnum(const_cast<char*>("colors"));
		if (colors_count < 0) colors_count = 0;

		// Default assumptions
		fgLevel = (colors_count >= 8) ? ColorLevel::BASIC8 : ColorLevel::NONE;
		bgLevel = (colors_count >= 8) ? ColorLevel::BASIC8 : ColorLevel::NONE;

		// Inspect setaf/setab for bright support
		char* setaf = tigetstr(const_cast<char*>("setaf"));
		char* setab = tigetstr(const_cast<char*>("setab"));

		if (seq_supports_bright(setaf) && colors_count >= 16)
				fgLevel = ColorLevel::FULL16;
		if (seq_supports_bright(setab) && colors_count >= 16)
				bgLevel = ColorLevel::FULL16;

		bool anycolor = (fgLevel != ColorLevel::NONE || bgLevel != ColorLevel::NONE);
		bool full16 = (fgLevel == ColorLevel::FULL16 && bgLevel == ColorLevel::FULL16);

		return {fgLevel, bgLevel, colors_count, anycolor, full16};
}

std::string colortxt(const std::string& txt, int fg, int bg) {
		if (colors_count < 0) detect_colors();
		if (fgLevel == ColorLevel::NONE && bgLevel == ColorLevel::NONE) return txt;

		std::ostringstream seq;
		bool has = false;

		if (fg >= 0) {
				int fidx = clamp_fg(fg);
				seq << fg_code_for(fidx);
				has = true;
		}
		if (bg >= 0) {
				if (has) seq << ";";
				int bidx = clamp_bg(bg);
				seq << bg_code_for(bidx);
				has = true;
		}

		if (!has) return txt;

		return "\033[" + seq.str() + "m" + txt + "\033[0m";
}

std::string colortxt(const std::string& txt, Color fg, Color bg) {
		int fg_val = (fg == Color::Default || fg == Color::Reset || fg == Color::Normal) ? -1 : static_cast<int>(fg);
		int bg_val = (bg == Color::Default || bg == Color::Reset || bg == Color::Normal) ? -1 : static_cast<int>(bg);
		return colortxt(txt, fg_val, bg_val);
}

std::string colorstr(const std::string& txt, Color fg, Color bg) {
		return colortxt(txt, fg, bg);
}

State get_state() {
		if (colors_count < 0) detect_colors();
		bool anycolor = (fgLevel != ColorLevel::NONE || bgLevel != ColorLevel::NONE);
		bool full16 = (fgLevel == ColorLevel::FULL16 && bgLevel == ColorLevel::FULL16);
		return {fgLevel, bgLevel, colors_count, anycolor, full16};
}

} // namespace colordetect

// Test comment

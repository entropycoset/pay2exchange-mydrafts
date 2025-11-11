#include "libstdpipeutil.hpp"

namespace stdpipeutil {

void stderr_msg(const std::string& message) {
		std::cerr << "\033[0m" << message << "\033[0m\n";
}

std::string make_reset_msg(const std::string& message) {
		return "\033[0m" + message + "\033[0m\n";
}

} // namespace stdpipeutil

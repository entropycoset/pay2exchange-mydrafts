#pragma once

#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring>
#include <errno.h>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>

namespace stdpipeutil {

// Template function to wrap libc functions and throw on error with errno text
template<typename T>
T check_syscall(T result, const char* syscall_name) {
		if (result == -1) {
				throw std::runtime_error(std::string(syscall_name) + " failed: " + std::strerror(errno));
		}
		return result;
}

// Helper function to write stderr messages with color reset protection
void stderr_msg(const std::string& message);

// Helper function to create color-reset protected stderr message
std::string make_reset_msg(const std::string& message);

} // namespace stdpipeutil

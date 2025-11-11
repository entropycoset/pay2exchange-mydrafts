#ifndef LIBCMDFORMAT_HPP
#define LIBCMDFORMAT_HPP

#include <string>
#include <stdexcept>
#include <iostream>

namespace libcmdformat {

enum class CmdFormat {
		cmdformat_raw,			// Send command as-is with newline (original behavior)
		cmdformat_v1lenend	// Send length;command;END format
};

const size_t max_cmd_len = 1024 * 1024; // 1MB max command length

// Custom assert macro similar to FC_ASSERT
#define CMD_ASSERT(condition, message) \
		do { \
				if (!(condition)) { \
						throw std::runtime_error(std::string("CMD_ASSERT failed: ") + (message)); \
				} \
		} while(0)

// General encode function that works with any format
std::string encode_command(const std::string& command, CmdFormat format);

// General decode function that works with any format
template<typename InputStream>
std::string decode_command(InputStream& input, CmdFormat format);

// Specific encode functions for each format
std::string encode_raw(const std::string& command);
std::string encode_v1lenend(const std::string& command);

// Specific decode functions for each format
template<typename InputStream>
std::string decode_raw(InputStream& input);

template<typename InputStream>
std::string decode_v1lenend(InputStream& input);

// Template implementations must be in header
template<typename InputStream>
std::string decode_command(InputStream& input, CmdFormat format) {
		switch (format) {
				case CmdFormat::cmdformat_raw:
						return decode_raw(input);
				case CmdFormat::cmdformat_v1lenend:
						return decode_v1lenend(input);
				default:
						throw std::runtime_error("Unknown command format");
		}
}

template<typename InputStream>
std::string decode_raw(InputStream& input) {
		std::string command;
		if (!std::getline(input, command)) {
				if (input.eof()) {
						throw std::runtime_error("EOF while reading raw command");
				} else {
						throw std::runtime_error("Failed to read raw command");
				}
		}
		return command;
}

// Helper function to read exactly N bytes, handling partial reads
template<typename InputStream>
void read_exact_bytes(InputStream& input, char* buffer, size_t bytes_to_read) {
		size_t total_read = 0;

		while (total_read < bytes_to_read) {
				size_t remaining = bytes_to_read - total_read;
				input.read(buffer + total_read, remaining);
				std::streamsize bytes_read_this_time = input.gcount();

				if (bytes_read_this_time == 0) {
						if (input.eof()) {
								throw std::runtime_error("Unexpected EOF while reading from stream - got " + std::to_string(total_read) + " bytes, expected " + std::to_string(bytes_to_read));
						}
						if (input.fail() || input.bad()) {
								throw std::runtime_error("Stream error while reading from stream - stream state: fail=" + std::to_string(input.fail()) + " bad=" + std::to_string(input.bad()));
						}
						// This shouldn't happen, but just in case
						throw std::runtime_error("read() returned 0 bytes but stream is not EOF or bad - unexpected state");
				}

				total_read += bytes_read_this_time;
		}
}

template<typename InputStream>
std::string decode_v1lenend(InputStream& input) {
		long long int cmd_len = -1;
		input >> cmd_len;

		if (input.fail()) {
				throw std::runtime_error("Reading cmd: failed to read command length");
		}
		if (cmd_len < 0) {
				throw std::runtime_error("Reading cmd: invalid zero/neg len of command");
		}
		if (cmd_len > static_cast<long long int>(max_cmd_len)) {
				throw std::runtime_error("Reading cmd: too long len of command");
		}

		char sep1;
		input >> sep1;
		if (input.fail()) {
				throw std::runtime_error("Reading cmd: failed to read separator");
		}
		if (sep1 != ';') {
				throw std::runtime_error("Reading cmd: invalid separator sep1");
		}

		// Use our helper function to guarantee reading exactly cmd_len bytes
		std::string theline(static_cast<size_t>(cmd_len), '\0');
		read_exact_bytes(input, &theline[0], static_cast<size_t>(cmd_len));

		// Also use helper for reading the endmark
		{
				const std::string exp_endmark = ";END";
				std::string given_endmark(exp_endmark.size(), '\0');
				read_exact_bytes(input, &given_endmark[0], exp_endmark.size());
				CMD_ASSERT(given_endmark == exp_endmark,
									 "invalid end-mark after the command");
		}

		return theline;
}

} // namespace libcmdformat

#endif // LIBCMDFORMAT_HPP

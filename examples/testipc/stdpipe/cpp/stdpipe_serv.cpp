#include <iostream>
#include <string>
#include <stdexcept>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <sstream>
#include <type_traits>
#include <limits>
#include <cstdlib>
#include <cerrno>
#include "libcmdformat/libcmdformat.hpp"
#include "libstdpipeutil/libstdpipeutil.hpp"
#include "libecul/ecul.hpp"

// Project name override for ECUL library
namespace ecul {
	std::string get_project_name() {
		return "StdPipeServApp";
	}
}

namespace utils {

template<typename TInt>
TInt parse_strict_integer(const std::string& input) {
		static_assert(std::is_integral<TInt>::value, "TInt must be an integral type");

		if (input.empty()) {
				ecul_stop("Empty string");
		}

		char* end = nullptr;
		errno = 0;

		// Generic normalization lambda
		auto normalize_and_compare = [&](TInt value) -> TInt {
				std::ostringstream oss;
				oss << value;
				std::string normalized = oss.str();

				if (input != normalized && input != ("+" + normalized)) {
						ecul_stop("Not normal integer string (junk besides the integer)");
				}
				return value;
		};

		if constexpr (std::is_unsigned<TInt>::value) {
				// --- Unsigned branch ---
				static_assert(std::numeric_limits<TInt>::max() <= std::numeric_limits<unsigned long long>::max(),
											"TInt too large for strtoull");

				unsigned long long raw = std::strtoull(input.c_str(), &end, 10);

				if (*end != '\0') {
						ecul_stop("Not normal integer string (junk besides the integer)");
				}
				if (errno == ERANGE || raw > std::numeric_limits<TInt>::max()) {
						ecul_stop("Value out of range for unsigned type");
				}

				return normalize_and_compare(static_cast<TInt>(raw));

		} else {
				// --- Signed branch ---
				static_assert(std::numeric_limits<TInt>::min() >= std::numeric_limits<long long>::min(),
											"TInt too small for strtoll");
				static_assert(std::numeric_limits<TInt>::max() <= std::numeric_limits<long long>::max(),
											"TInt too large for strtoll");

				long long raw = std::strtoll(input.c_str(), &end, 10);

				if (*end != '\0') {
						ecul_stop("Not normal integer string (junk besides the integer)");
				}
				if (errno == ERANGE || raw < std::numeric_limits<TInt>::min() || raw > std::numeric_limits<TInt>::max()) {
						ecul_stop("Value out of range for signed type");
				}

				return normalize_and_compare(static_cast<TInt>(raw));
		}
}

}

class StdPipeServer {
private:
		using fd_source = boost::iostreams::file_descriptor_source;
		using fd_sink = boost::iostreams::file_descriptor_sink;
		using fd_stream_in = boost::iostreams::stream<fd_source>;
		using fd_stream_out = boost::iostreams::stream<fd_sink>;

		std::unique_ptr<fd_stream_in> cmd_in_file;	 // command input pipe
		std::unique_ptr<fd_stream_out> cmd_out_file; // command output pipe
		libcmdformat::CmdFormat cmd_format = libcmdformat::CmdFormat::cmdformat_v1lenend; // Use v1lenend format

public:
		StdPipeServer(int cmd_in_fd, int cmd_out_fd) {
				ecul_log_info("Program starting - StdPipe Server initializing with FDs: " +
																std::to_string(cmd_in_fd) + ", " + std::to_string(cmd_out_fd));

				// Create boost::iostreams from native file descriptors
				try {
						cmd_in_file = std::make_unique<fd_stream_in>(cmd_in_fd, boost::iostreams::never_close_handle);
						if (!cmd_in_file->is_open()) {
								ecul_stop("Failed to open command input pipe (FD " + std::to_string(cmd_in_fd) + ")");
						}

						cmd_out_file = std::make_unique<fd_stream_out>(cmd_out_fd, boost::iostreams::never_close_handle);
						if (!cmd_out_file->is_open()) {
								ecul_stop("Failed to open command output pipe (FD " + std::to_string(cmd_out_fd) + ")");
						}
				} catch (const std::exception& e) {
						ecul_log_erro("Error creating boost::iostreams from FDs: " + std::string(e.what()));
						throw;
				}

				ecul_log_info("Program starting - StdPipe Server initialized successfully");
		}

		void send_reply(const std::string& reply) {
				ecul_log_info("Program sending reply: " + reply);

				// Use libcmdformat to encode the reply
				std::string formatted_reply = libcmdformat::encode_command(reply, cmd_format);

				cmd_out_file->write(formatted_reply.c_str(), formatted_reply.length());
				if (cmd_out_file->fail()) {
						ecul_stop("Failed to write reply to command output pipe");
				}
				cmd_out_file->flush();
		}

		void main_loop() {
				while (true) {
						ecul_log_info("Program waiting for command...");

						try {
								// Use libcmdformat to decode the command
								std::string command = libcmdformat::decode_command(*cmd_in_file, cmd_format);

								ecul_log_info("Program getting a command: '" + command + "'");

								if (command == "ping") {
										send_reply("pong");
								} else if (command == "quit") {
										ecul_log_info("Program received quit command - exiting loop");
										send_reply("goodbye");
										break;
								} else if (command.substr(0, 6) == "sleep ") {
										// Handle "sleep N" command where N is milliseconds
										try {
												std::string ms_str = command.substr(6);
												if (ms_str.empty()) {
														send_reply("sleep command requires milliseconds parameter");
														continue;
												}

												unsigned int milliseconds = utils::parse_strict_integer<unsigned int>(ms_str);

												// Limit maximum sleep to prevent abuse (10 seconds = 10000ms)
												if (milliseconds > 10000) {
														send_reply("sleep duration limited to 10000 milliseconds maximum");
														continue;
												}

												ecul_log_info("Program sleeping for " + std::to_string(milliseconds) + " milliseconds");
												std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
												ecul_log_info("Program finished sleeping");

												send_reply("slept " + std::to_string(milliseconds) + " ms");
										} catch (const std::exception& e) {
												ecul_log_erro("Error parsing sleep parameter: " + std::string(e.what()));
												send_reply("invalid sleep parameter: " + std::string(e.what()));
										}
								} else {
										ecul_log_warn("Unknown command received: '" + command + "'");
										send_reply("command unknown");
								}
						} catch (const std::exception& e) {
								ecul_log_erro("Error reading/decoding command: " + std::string(e.what()));
								// Check if it's EOF or a real error
								if (cmd_in_file->eof()) {
										ecul_log_info("Program detected end of input - exiting loop");
										break;
								} else {
										// For other errors, we might want to continue or exit depending on the error
										ecul_log_info("Continuing after command decode error...");
										continue;
								}
						}
				}

				ecul_log_info("Program exiting main loop");
		}
};

void print_usage(const std::string& program_name) {
		// Use ECUL logging for usage information
		ecul_log_info("StdPipe Server");
		ecul_log_info("");
		ecul_log_info("Usage: " + program_name + " <cmd_in_fd> <cmd_out_fd>");
		ecul_log_info("");
		ecul_log_info("Arguments:");
		ecul_log_info("	cmd_in_fd		 File descriptor number for command input pipe (required)");
		ecul_log_info("	cmd_out_fd	 File descriptor number for command output pipe (required)");
		ecul_log_info("");
		ecul_log_info("Description:");
		ecul_log_info("	Server process that communicates via anonymous pipes using the provided");
		ecul_log_info("	file descriptors. Typically spawned by stdpipe_back controller.");
		ecul_log_info("");
		ecul_log_info("Commands:");
		ecul_log_info("	ping				 Responds with 'pong'");
		ecul_log_info("	sleep N			 Sleeps for N milliseconds (max 10000) then responds");
		ecul_log_info("	quit				 Responds with 'goodbye' and exits");
		ecul_log_info("	<unknown>		 Responds with 'command unknown'");
		ecul_log_info("");
		ecul_log_info("Note:");
		ecul_log_info("	This program is usually not run directly but launched by stdpipe_back");
		ecul_log_info("	which creates the pipes and passes the file descriptor numbers.");
		ecul_log_info("");
}

int main(int argc, char* argv[]) {
		// Initialize ECUL colors for thread-safe logging
		ecul::init_colors();
		
		try {
				// First convert argv to safe vector
				std::vector<std::string> argvect;
				for (int i = 0; i < argc; ++i) {
						if (argv[i] == nullptr) {
								ecul_stop("Null argv element at index " + std::to_string(i));
						}
						argvect.push_back(std::string(argv[i]));
				}

				// Check for --help
				if (argvect.size() > 1 && argvect.at(1) == "--help") {
						print_usage(argvect.at(0));
						return 0;
				}

				// Expect command line arguments for the two pipe file descriptors
				if (argvect.size() != 3) {
						print_usage(argvect.at(0));
						ecul_log_erro("Error: Expected exactly 2 file descriptors for anonymous pipes");
						return 1;
				}

				int cmd_in_fd = utils::parse_strict_integer<int>(argvect.at(1));
				int cmd_out_fd = utils::parse_strict_integer<int>(argvect.at(2));
				ecul_log_info("Will talk CMD on: cmd-in fd " + std::to_string(cmd_in_fd) + ", cmd-out fd " + std::to_string(cmd_out_fd));

				if (cmd_in_fd < 0 || cmd_out_fd < 0) {
						ecul_stop("Invalid file descriptor numbers");
				}

				StdPipeServer server(cmd_in_fd, cmd_out_fd);
				server.main_loop();
				ecul_log_info("Program exiting normally");
				return 0;
		} catch (const std::exception& e) {
				ecul_log_erro("Exception caught: " + std::string(e.what()));
				return 1;
		// UNSAFE_LINTER_IGNORE_CATCH_ALL
		// TODO check is this OK to catch-all stop. XXX security
		} catch (...) {
				ecul_log_erro("Unknown exception caught");
				return 2;
		}
}

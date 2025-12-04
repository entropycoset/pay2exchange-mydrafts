// b
#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <boost/process.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

#include <ctime>
#include <limits>

#include "libvalidcolor/libvalidcolor.hpp"
#include "libcmdformat/libcmdformat.hpp"
#include "libstdpipeutil/libstdpipeutil.hpp"
#include "libecul/ecul.hpp"

namespace bp = boost::process;

// Project name override for ECUL library
namespace ecul {
	std::string get_project_name() {
		return "StdPipeBackApp";
	}
}


enum class StdOutErrMode {
		OutErrModeHide,			// Redirect stdout/stderr to /dev/null
		OutErrModeCapture,	// Capture stdout/stderr via pipes
		OutErrModeDirect		// Current behavior - inherit parent's stdout/stderr
};

// Use libcmdformat's CmdFormat enum
using libcmdformat::CmdFormat;

class my_fd {
private:
		int m_fd;		 // my file descriptor, e.g. from open or one out of pipe()
		bool m_owned; // tells if this object owns this FD and should close it

public:
		my_fd() : m_fd(-1), m_owned(false) {}

		my_fd(int _id, bool _owned) : m_fd(_id), m_owned(_owned) {}

		virtual ~my_fd() {
				close();
		}

		/// Closes FD if it is owned by us and opened
		void close() {
				if (m_owned && m_fd >= 0) {
						// Note: We don't use check_syscall for close() because it might legitimately
						// fail (e.g., if already closed) and we want to proceed with cleanup anyway
						if (::close(m_fd) == -1) {
								// Log but don't throw - we're in cleanup mode
								ecul_log_warn("close() failed for FD " + std::to_string(m_fd) + ": " + std::strerror(errno));
						}
						m_owned = false;
						m_fd = -1;
				}
		}

		bool is_open() const { return m_fd>=0; }

		/// Get the file descriptor, it will be valid FD (otherwise we throw)
		int get_fd() const {
			if (!is_open()) {
				throw ecul_stop("Tried to use invalid/closed FD");
			}
			return m_fd;
		}

		// Set the file descriptor and ownership. The _fd must be valid (>=-1) otherwise throws and leave object unchanged
		void set_fd(int _fd, bool _owned) {
				if (!(_fd>=0)) {
					throw ecul_stop("Invalid fd being set");
				}
				close(); // Close existing if owned
				m_fd = _fd;
				m_owned = _owned;
		}
};

class my_pipe {
private:
		my_fd m_read;
		my_fd m_write;

public:
		my_pipe()=default;
		virtual ~my_pipe()=default;

		// Function spawn() calls pipe() and save the FDs
		void spawn() {
				int pipe_fds[2];
				stdpipeutil::check_syscall(pipe(pipe_fds), "pipe");
				// we own both ends
				m_read.set_fd(pipe_fds[0], true);		// Read end
				m_write.set_fd(pipe_fds[1], true);	// Write end
		}

		// New API: Return references to the my_fd objects
		my_fd& side_read() {
				return m_read;
		}

		const my_fd& side_read() const {
				return m_read;
		}

		my_fd& side_write() {
				return m_write;
		}

		const my_fd& side_write() const {
				return m_write;
		}
};

class StdPipeController {
private:
		my_pipe cmd_pipe;					// Command pipe (parent writes, child reads)
		my_pipe resp_pipe;				// Response pipe (child writes, parent reads)
		my_pipe child_stdout_pipe; // For capturing child stdout (secure anonymous pipe)
		my_pipe child_stderr_pipe; // For capturing child stderr (secure anonymous pipe)
		bp::child server_process; // The stdpipe_serv process
		StdOutErrMode m_stdouterr_mode;
		CmdFormat m_cmdformat = CmdFormat::cmdformat_v1lenend; // Always use v1lenend format
		std::string accumulated_stdout;
		std::string accumulated_stderr;

		// Timeout configuration
		std::chrono::seconds max_timeout{5};
		std::chrono::milliseconds warn_timeout{2500};

		// Helper lambda for timed pipe operations with timeout and warning
		template<typename Operation>
		auto timed_pipe_operation(const std::string& operation_name, Operation&& op) {
				auto start_time = std::chrono::steady_clock::now();

				auto result = std::forward<Operation>(op)();

				auto end_time = std::chrono::steady_clock::now();
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

				if (duration >= warn_timeout) {
						double seconds = duration.count() / 1000.0;
						ecul_log_warn("operation took long " + operation_name + " - " + std::to_string(seconds) + " seconds");
				}

				return result;
		}

public:
		StdPipeController(const std::string& server_path, const std::string& cleanup_exec_prog = "",
			StdOutErrMode stdouterr_mode = StdOutErrMode::OutErrModeDirect,
			const std::string& mode = "", const std::vector<std::string>& server_args = {})
				: m_stdouterr_mode(stdouterr_mode) {

					ecul_info("START CONSTR XXX");

				for (const auto & one : server_args) ecul_info((std::ostringstream{}<<"Preparing controller with args ["<<one<<"]").str());

				// Always use v1lenend format now that both client and server support it
				m_cmdformat = CmdFormat::cmdformat_v1lenend;
				if (server_path.empty()) {
						throw ecul_stop("Server path cannot be empty");
				}

				ecul_log_info("StdPipeController: Starting server process: " + server_path);
				if (!cleanup_exec_prog.empty()) {
						ecul_log_info("StdPipeController: Using cleanup_exec: " + cleanup_exec_prog);
				} else {
						ecul_log_warn("StdPipeController: No cleanup_exec program specified - server will run in current environment");
				}

				try {
						// Create pipes using my_pipe class
						cmd_pipe.spawn();
						resp_pipe.spawn();

						// Create stdout/stderr capture pipes if needed
						if (m_stdouterr_mode == StdOutErrMode::OutErrModeCapture) {
								child_stdout_pipe.spawn();
								child_stderr_pipe.spawn();

								// Set non-blocking mode on read ends
								int flags = stdpipeutil::check_syscall(fcntl(child_stdout_pipe.side_read().get_fd(), F_GETFL, 0), "fcntl F_GETFL");
								stdpipeutil::check_syscall(fcntl(child_stdout_pipe.side_read().get_fd(), F_SETFL, flags | O_NONBLOCK), "fcntl F_SETFL");

								flags = stdpipeutil::check_syscall(fcntl(child_stderr_pipe.side_read().get_fd(), F_GETFL, 0), "fcntl F_GETFL");
								stdpipeutil::check_syscall(fcntl(child_stderr_pipe.side_read().get_fd(), F_SETFL, flags | O_NONBLOCK), "fcntl F_SETFL");

								ecul_log_info("StdPipeController: Created secure anonymous pipes for stdout/stderr capture");
						}

						ecul_log_info("StdPipeController: Created pipes - cmd_pipe[" + std::to_string(cmd_pipe.side_read().get_fd()) + "," + std::to_string(cmd_pipe.side_write().get_fd())
											+ "], resp_pipe[" + std::to_string(resp_pipe.side_read().get_fd()) + "," + std::to_string(resp_pipe.side_write().get_fd()) + "]");

						// Convert FD numbers to strings for passing as arguments
						std::string cmd_fd_str = std::to_string(cmd_pipe.side_read().get_fd());
						std::string resp_fd_str = std::to_string(resp_pipe.side_write().get_fd());

						ecul_log_info("StdPipeController: Will pass FDs " + cmd_fd_str + " and " + resp_fd_str + " to child process");

						// Prepare arguments vector
						std::vector<std::string> all_args;

						// Add server-specific arguments first (for cli_wallet)
						for (const auto& arg : server_args) {
								all_args.push_back(arg);
						}

						// Add cmd-pipe argument for cli_wallet or FD args for stdpipe_serv
						// TODO BADAI
						if (!server_args.empty()) {
								// For cli_wallet, use --cmd-pipe XXX,YYY format
								all_args.push_back("--cmd-pipe");
								all_args.push_back(cmd_fd_str + "," + resp_fd_str);
						} else {
								// For stdpipe_serv, use individual FD arguments
								all_args.push_back(cmd_fd_str);
								all_args.push_back(resp_fd_str);
						}

						// Debug: print all arguments
						std::string args_str = "StdPipeController: Starting with args:";
						for (const auto& arg : all_args) {
								args_str += " '" + arg + "'";
						}
						ecul_info(args_str);

						// Start the server process - setup redirection based on stdouterr mode
						if (cleanup_exec_prog.empty()) {
								// Direct execution
								switch (m_stdouterr_mode) {
										case StdOutErrMode::OutErrModeHide: {
												std::vector<std::string> args_for_bp = {server_path};
												args_for_bp.insert(args_for_bp.end(), all_args.begin(), all_args.end());
												server_process = bp::child(
														args_for_bp,
														bp::std_in.close(),
														bp::std_out > "/dev/null",
														bp::std_err > "/dev/null"
												);
												break;
										}
										case StdOutErrMode::OutErrModeCapture: {
												// Use manual fork/exec approach for precise control
												pid_t pid = stdpipeutil::check_syscall(fork(), "fork");
												if (pid == 0) {
														// Child process
														stdpipeutil::check_syscall(dup2(child_stdout_pipe.side_write().get_fd(), STDOUT_FILENO), "dup2 stdout");
														stdpipeutil::check_syscall(dup2(child_stderr_pipe.side_write().get_fd(), STDERR_FILENO), "dup2 stderr");

														// For stdpipe_serv, ensure FDs are at expected positions
														if (server_args.empty()) {
																stdpipeutil::check_syscall(dup2(cmd_pipe.side_read().get_fd(), atoi(cmd_fd_str.c_str())), "dup2 cmd");
																stdpipeutil::check_syscall(dup2(resp_pipe.side_write().get_fd(), atoi(resp_fd_str.c_str())), "dup2 resp");
														}

														// Close all our pipe ends in child
														cmd_pipe.side_write().close();
														resp_pipe.side_read().close();
														child_stdout_pipe.side_read().close();
														child_stderr_pipe.side_read().close();

														// Prepare execv arguments
														std::vector<const char*> argv_exec;
														argv_exec.push_back(server_path.c_str());
														for (const auto& arg : all_args) {
																argv_exec.push_back(arg.c_str());
														}
														argv_exec.push_back(nullptr);

														execv(server_path.c_str(), const_cast<char* const*>(argv_exec.data()));
														exit(1); // If execv fails
												} else if (pid > 0) {
														// Parent process - wrap pid in boost::process child
														server_process = bp::child(pid);
												} else {
														throw ecul_stop("Failed to fork child process");
												}
												break;
										}
										case StdOutErrMode::OutErrModeDirect:
										default: {
												std::vector<std::string> args_for_bp = {server_path};
												args_for_bp.insert(args_for_bp.end(), all_args.begin(), all_args.end());
												server_process = bp::child(
														args_for_bp,
														bp::std_in.close()
												);
												break;
										}
								}
						} else {
								// Execute via cleanup_exec with cleanup options
								std::string clean_fd_except = "0,1,2," + cmd_fd_str + "," + resp_fd_str;
								if (m_stdouterr_mode == StdOutErrMode::OutErrModeCapture) {
										clean_fd_except += "," + std::to_string(child_stdout_pipe.side_write().get_fd()) +
																			 "," + std::to_string(child_stderr_pipe.side_write().get_fd());
								}
								std::string clean_env_except = "HOME,USER";

								ecul_log_info("StdPipeController: Running via cleanup_exec with clean-fd-except=" + clean_fd_except
													+ " and clean-env-except=" + clean_env_except);

								switch (m_stdouterr_mode) {
										case StdOutErrMode::OutErrModeHide:
												ecul_info("Mode: hide");
												server_process = bp::child(
														cleanup_exec_prog,
														"--run",
														"--clean-fd-except", clean_fd_except,
														"--clean-env-except", clean_env_except,
														server_path,
														cmd_fd_str,
														resp_fd_str,
														bp::std_in.close(),
														bp::std_out > "/dev/null",
														bp::std_err > "/dev/null"
												);
												break;
										case StdOutErrMode::OutErrModeCapture: {
												ecul_info("Mode: capture");
												// For cleanup_exec path, use manual fork/exec as well
												pid_t pid = stdpipeutil::check_syscall(fork(), "fork");
												if (pid == 0) {
														// Child process
														stdpipeutil::check_syscall(dup2(child_stdout_pipe.side_write().get_fd(), STDOUT_FILENO), "dup2 stdout");
														stdpipeutil::check_syscall(dup2(child_stderr_pipe.side_write().get_fd(), STDERR_FILENO), "dup2 stderr");
														stdpipeutil::check_syscall(dup2(cmd_pipe.side_read().get_fd(), atoi(cmd_fd_str.c_str())), "dup2 cmd");
														stdpipeutil::check_syscall(dup2(resp_pipe.side_write().get_fd(), atoi(resp_fd_str.c_str())), "dup2 resp");

														// Close all our pipe ends in child
														cmd_pipe.side_write().close();
														resp_pipe.side_read().close();
														child_stdout_pipe.side_read().close();
														child_stderr_pipe.side_read().close();

														execl(cleanup_exec_prog.c_str(), cleanup_exec_prog.c_str(),
																	"--run", "--clean-fd-except", clean_fd_except.c_str(),
																	"--clean-env-except", clean_env_except.c_str(),
																	server_path.c_str(), cmd_fd_str.c_str(), resp_fd_str.c_str(),
																	(char*)nullptr);
														exit(1); // If execl fails
												} else if (pid > 0) {
														// Parent process - wrap pid in boost::process child
														server_process = bp::child(pid);
												} else {
														throw ecul_stop("Failed to fork child process");
												}
												break;
										}
										case StdOutErrMode::OutErrModeDirect:
												ecul_info("Mode: Direct");
												server_process = bp::child(
														cleanup_exec_prog,
														"--run",
														"--clean-fd-except", clean_fd_except,
														"--clean-env-except", clean_env_except,
														server_path,
														cmd_fd_str,
														resp_fd_str,
														"--",
														all_args,

														bp::std_in.close()
												);
												break;
										default:
												throw ecul_erro_runtime("Unknown mode");
								}
						}

						// Close the child's ends in parent
						cmd_pipe.side_read().close();		// Child uses this for reading
						resp_pipe.side_write().close(); // Child uses this for writing

						// Close write ends of capture pipes in parent (child writes to them)
						if (m_stdouterr_mode == StdOutErrMode::OutErrModeCapture) {
								child_stdout_pipe.side_write().close();
								child_stderr_pipe.side_write().close();
						}

						ecul_log_info("StdPipeController: Created anonymous pipes - cmd input FD " + cmd_fd_str
											+ ", response output FD " + resp_fd_str);

						// Give the child a moment to start and check if it's still running
						std::this_thread::sleep_for(std::chrono::milliseconds(100));

						if (!server_process.running()) {
								throw ecul_stop("Failed to start server process");
						}

						ecul_log_info("StdPipeController: Server process started successfully");

				} catch (const std::exception& e) {
						ecul_log_erro("Error starting server process: " + std::string(e.what()));
						throw;
				}
		}

		~StdPipeController() {
				if (server_process.running()) {
						ecul_log_info("StdPipeController: Terminating server process");
						server_process.terminate();
						server_process.wait();
				}
				// The my_pipe destructors will automatically close owned FDs
		}

		// Timeout configuration methods
		void set_timeouts(int max_timeout_seconds) {
				max_timeout = std::chrono::seconds(max_timeout_seconds);
				warn_timeout = std::chrono::milliseconds(max_timeout_seconds * 500); // Half of max_timeout
		}

		void set_timeouts(int max_timeout_seconds, int warn_timeout_milliseconds) {
				max_timeout = std::chrono::seconds(max_timeout_seconds);
				warn_timeout = std::chrono::milliseconds(warn_timeout_milliseconds);
		}

		void send_command(const std::string& command) {
				// Use ECUL logging for command sending (info level)
				ecul_log_info("StdPipeController: Sending command: " + command);

				timed_pipe_operation("sending command", [&]() {
						// Add timeout using select() for write operation
						fd_set write_fds;
						FD_ZERO(&write_fds);

						int fd = cmd_pipe.side_write().get_fd();
						if (fd < 0 || fd >= FD_SETSIZE) {
								throw ecul_stop("Invalid file descriptor for write select(): " + std::to_string(fd));
						}

						FD_SET(fd, &write_fds);

						struct timeval timeout_val;
						timeout_val.tv_sec = max_timeout.count();
						timeout_val.tv_usec = 0;

						int select_result = stdpipeutil::check_syscall(select(fd + 1, nullptr, &write_fds, nullptr, &timeout_val), "select write");

						if (select_result == 0) {
								throw ecul_stop("Timeout writing command to pipe (" + std::to_string(max_timeout.count()) + " seconds)");
						}

						if (!FD_ISSET(fd, &write_fds)) {
								throw ecul_stop("select() returned but FD is not ready for writing");
						}

						// Use libcmdformat to encode the command
						std::string formatted_command = libcmdformat::encode_command(command, m_cmdformat);

						// Show raw encoded command being sent
						ecul_log_info("StdPipeController: Sending RAW: " + formatted_command);

						ssize_t bytes_written = stdpipeutil::check_syscall(write(fd, formatted_command.c_str(), formatted_command.length()), "write");

						if (bytes_written != static_cast<ssize_t>(formatted_command.length())) {
								throw ecul_stop("Partial write to command pipe: " + std::to_string(bytes_written) +
																				" of " + std::to_string(formatted_command.length()) + " bytes written");
						}
						ecul_log_info("StdPipe...: written the command into pipe - done sending.");

						return bytes_written;
				});
		}

		std::string read_response() {
				return timed_pipe_operation("reading reply", [&]() -> std::string {
						// Add timeout using select() with proper error handling
						fd_set read_fds;
						FD_ZERO(&read_fds);

						int fd = resp_pipe.side_read().get_fd();
						if (fd < 0 || fd >= FD_SETSIZE) {
								throw ecul_stop("Invalid file descriptor for select(): " + std::to_string(fd));
						}

						FD_SET(fd, &read_fds);

						struct timeval timeout_val;
						timeout_val.tv_sec = max_timeout.count();
						timeout_val.tv_usec = 0;

						ecul_log_info("StdPipe...: Reading the reply...");
						int select_result = stdpipeutil::check_syscall(select(fd + 1, &read_fds, nullptr, nullptr, &timeout_val), "select");
						ecul_log_info("StdPipe...: Reading the reply - done...");

						if (select_result == 0) {
								throw ecul_stop("Timeout waiting for server response (" + std::to_string(max_timeout.count()) + " seconds)");
						}

						// Verify the FD is actually ready for reading
						if (!FD_ISSET(fd, &read_fds)) {
								throw ecul_stop("select() returned but FD is not ready for reading");
						}

						// Create a proper stream from the file descriptor and let libcmdformat handle the reading
						// This avoids buffer size limitations and handles v1lenend format correctly
						boost::iostreams::file_descriptor_source fd_source(fd, boost::iostreams::never_close_handle);
						boost::iostreams::stream<boost::iostreams::file_descriptor_source> fd_stream(fd_source);

						ecul_log_info("StdPipe...: Reading the reply via stream...");

						// Let libcmdformat decode directly from the stream - this handles large responses correctly
						std::string decoded_response = libcmdformat::decode_command(fd_stream, m_cmdformat);

						ecul_log_info("StdPipe...: Successfully decoded response of length: " + std::to_string(decoded_response.length()));

						// Show a truncated version of the response for debugging (first 200 chars)
						std::string display_response = decoded_response.length() > 200
								? decoded_response.substr(0, 200) + "...[truncated]"
								: decoded_response;

						// Log decoded response
						ecul_log_info("StdPipeController: Decoded response: " + display_response);
						return decoded_response;
				});
		}

		std::string send_command_and_read_reply(const std::string& command) {
				handle_child(); // Capture any pending child output before sending
				send_command(command);
				std::string response = read_response();
				handle_child(); // Capture any child output after command processing
				return response;
		}

		void handle_child() {
				if (m_stdouterr_mode != StdOutErrMode::OutErrModeCapture) return;

				char buffer[4096];

				// Non-blocking read from child stdout pipe
				if (child_stdout_pipe.side_read().is_open()) {
						ssize_t bytes = read(child_stdout_pipe.side_read().get_fd(), buffer, sizeof(buffer) - 1);
						if (bytes > 0) {
								buffer[bytes] = '\0';
								accumulated_stdout += buffer;
						}
						// bytes == -1 with errno EAGAIN is normal for non-blocking read with no data
				}

				// Non-blocking read from child stderr pipe
				if (child_stderr_pipe.side_read().is_open()) {
						ssize_t bytes = read(child_stderr_pipe.side_read().get_fd(), buffer, sizeof(buffer) - 1);
						if (bytes > 0) {
								buffer[bytes] = '\0';
								accumulated_stderr += buffer;
						}
						// bytes == -1 with errno EAGAIN is normal for non-blocking read with no data
				}
		}

		void display_and_clear_captured() {
				if (m_stdouterr_mode != StdOutErrMode::OutErrModeCapture) return;

				if (!accumulated_stdout.empty()) {
						// Use ECUL logging for child stdout
						ecul_log_info("[CHILD STDOUT] " + accumulated_stdout);
						accumulated_stdout.clear();
				}
				if (!accumulated_stderr.empty()) {
						// Use ECUL logging for child stderr
						ecul_log_warn("[CHILD STDERR] " + accumulated_stderr);
						accumulated_stderr.clear();
				}
		}

		void run_cli_mode() {
				ecul_log_info("StdPipeController: Starting CLI interactive mode");
				ecul_log_info("Interactive CLI mode. Type 'quit', 'abort', or 'abort2' to exit.");

				try {
						std::string line;
						while (true) {
								// Display any accumulated child output before prompt
								handle_child();
								display_and_clear_captured();

								std::cout << "> ";
								std::cout.flush();

								if (!std::getline(std::cin, line)) {
										// EOF reached (Ctrl+D)
										ecul_log_info("EOF reached, sending quit and exiting.");
										std::string response = send_command_and_read_reply("quit");
										ecul_log_info("Server response: " + response);
										break;
								}

								if (line == "quit") {
										// Send quit, wait for response, then exit
										std::string response = send_command_and_read_reply("quit");
										ecul_log_info("Server response: " + response);
										break;
								} else if (line == "abort") {
										// Send quit, then exit without waiting for response
										send_command("quit");
										ecul_log_info("Sent quit command, exiting without waiting for response.");
										break;
								} else if (line == "abort2") {
										// Exit immediately without sending quit - terminate server
										ecul_log_info("Exiting immediately without sending quit.");
										if (server_process.running()) {
												server_process.terminate();
										}
										break;
								} else {
										// Send the command and display response
										try {
												std::string response = send_command_and_read_reply(line);
												ecul_log_info("Server response: " + response);
										} catch (const std::exception& e) {
												ecul_log_erro("Error communicating with server: " + std::string(e.what()));
												break;
										}
								}
						}

						// Close our end of the pipes
						cmd_pipe.side_write().close();
						resp_pipe.side_read().close();

						// Wait for server to exit
						if (server_process.running()) {
								server_process.wait();
						}

						ecul_log_info("CLI mode completed");

				} catch (const std::exception& e) {
						ecul_log_erro("CLI mode error: " + std::string(e.what()));
						throw;
				}
		}

		void run_test() {
				ecul_log_info("StdPipeController: Starting communication test");

				try {
						// Test 1: Send ping, expect pong
						std::string response1 = send_command_and_read_reply("ping");
						if (response1 != "pong") {
								ecul_log_erro("Expected 'pong' but got: '" + response1 + "'");
								return;
						}
						ecul_log_info("✓ Ping test passed");

						// Display any captured child output after ping test
						handle_child();
						display_and_clear_captured();

						// Small delay
						std::this_thread::sleep_for(std::chrono::milliseconds(100));

						// Test 2: Send quit
						std::string response2 = send_command_and_read_reply("quit");
						if (response2 != "goodbye") {
								ecul_log_erro("Expected 'goodbye' but got: '" + response2 + "'");
								return;
						}
						ecul_log_info("✓ Quit test passed");

						// Display any final captured child output
						handle_child();
						display_and_clear_captured();

						// Close our end of the pipes
						cmd_pipe.side_write().close();
						resp_pipe.side_read().close();

						// Wait for server to exit
						server_process.wait();

						if (server_process.exit_code() != 0) {
								ecul_log_erro("Server process exited with code: " +
																			 std::to_string(server_process.exit_code()));
								return;
						}

						ecul_log_info("✓ All tests passed successfully");

				} catch (const std::exception& e) {
						ecul_log_erro("✗ Test failed: " + std::string(e.what()));
						throw;
				}
		}

		void run_demo_gdgp() {
				ecul_log_info("StdPipeController: Starting demo mode - get_dynamic_global_properties");

				try {
						set_timeouts(15);

						// Give cli_wallet some time to connect to RPC endpoint
						ecul_log_info("StdPipeController: Waiting for cli_wallet to initialize and connect to RPC...");
						std::this_thread::sleep_for(std::chrono::seconds(5));

						// Send get_dynamic_global_properties command
						ecul_log_info("StdPipeController: Sending get_dynamic_global_properties command...");
						std::string response = send_command_and_read_reply("get_dynamic_global_properties");

						// Parse JSON response
						nlohmann::json json_response;
						try {
								json_response = nlohmann::json::parse(response);
						} catch (const nlohmann::json::parse_error& e) {
								ecul_log_erro("Failed to parse JSON response: " + std::string(e.what()));
								return;
						}

						// Print nicely formatted JSON
						ecul_log_info("JSON Response (formatted):");
						ecul_log_info(json_response.dump(4));

						// Extract the 3 required values
						try {
								auto head_block_number = json_response["head_block_number"].get<int>();
								auto head_block_id = json_response["head_block_id"].get<std::string>();
								auto time = json_response["time"].get<std::string>();

								ecul_log_info("Extracted values:");
								ecul_log_info("head_block_number: " + std::to_string(head_block_number));
								ecul_log_info("head_block_id: " + head_block_id);
								ecul_log_info("time: " + time);

						} catch (const nlohmann::json::exception& e) {
								ecul_log_erro("Failed to extract required JSON fields: " + std::string(e.what()));
								return;
						}


						{
								std::string response = send_command_and_read_reply("get_global_properties");
								nlohmann::json json_response;
								try {
										json_response = nlohmann::json::parse(response);
								} catch (const nlohmann::json::parse_error& e) {
										ecul_log_erro("Failed to parse JSON response: " + std::string(e.what()));
										return;
								}

								try {
										ecul_log_info("Active Witnesses:");
										const auto & data = json_response;
										for (const auto& witness_id : data["active_witnesses"]) {
												ecul_log_info(witness_id.get<std::string>());
										}
								} catch (const nlohmann::json::exception& e) {
										ecul_log_erro("Failed to extract required JSON fields: " + std::string(e.what()));
										return;
								}

						}

						// Send quit command - handle gracefully as cli_wallet may close pipe without formatted response
						ecul_log_info("StdPipeController: Sending quit command...");
						try {
								std::string quit_response = send_command_and_read_reply("quit");
								ecul_log_info("✓ Demo completed, server response to quit: " + quit_response);
						} catch (const std::exception& e) {
								// cli_wallet often closes the pipe without sending a proper formatted response when quitting
								ecul_log_info("✓ Demo completed (quit response not received - wallet closed connection, which is expected)");
						}

						// Display any final captured child output
						handle_child();
						display_and_clear_captured();

						// Close our end of the pipes
						cmd_pipe.side_write().close();
						resp_pipe.side_read().close();

						// Wait for server to exit
						server_process.wait();

						if (server_process.exit_code() != 0) {
								ecul_log_erro("Server process exited with code: " +
																			 std::to_string(server_process.exit_code()));
								return;
						}

						ecul_log_info("✓ Demo mode completed successfully");

				} catch (const std::exception& e) {
						ecul_log_erro("✗ Demo mode failed: " + std::string(e.what()));
						throw;
				}
		}
};

void print_usage(const std::string& program_name) {
		// Use ecul_log_info for usage information
		ecul_log_info("StdPipe Backend Controller");
		ecul_log_info("");
		ecul_log_info("Usage: " + program_name + " <mode> [submode] [stdouterr] [server_path] [cleanup_exec_prog]");
		ecul_log_info("");
		ecul_log_info("Arguments:");
		ecul_log_info("	mode							Operation mode: 'test', 'demo', or 'cli'");
		ecul_log_info("	submode						Optional submode string (default: empty)");
		ecul_log_info("	stdouterr					Child stdout/stderr handling: 'direct', 'hide', or 'capture' (default: direct)");
		ecul_log_info("	server_path				Path to stdpipe_serv executable (default: ./stdpipe_serv)");
		ecul_log_info("	cleanup_exec_prog Optional path to clean_exec program for environment cleanup");
		ecul_log_info("");
		ecul_log_info("Modes:");
		ecul_log_info("	test							Run automated ping/quit test (original behavior)");
		ecul_log_info("	demo							Demo mode with submodes:");
		ecul_log_info("										- demo1/gdgp: Send get_dynamic_global_properties, parse JSON, extract values");
		ecul_log_info("										- (empty): Same as test mode");
		ecul_log_info("	cli								Interactive command-line interface");
		ecul_log_info("");
		ecul_log_info("StdOutErr Modes:");
		ecul_log_info("	direct						Child output goes directly to terminal (default)");
		ecul_log_info("	hide							Child output is redirected to /dev/null (hidden)");
		ecul_log_info("	capture						Child output is captured and shown before CLI prompts");
		ecul_log_info("");
		ecul_log_info("CLI Mode Commands:");
		ecul_log_info("	<any text>				Send command to server and display response");
		ecul_log_info("	quit							Send quit to server, wait for response, then exit");
		ecul_log_info("	abort							Send quit to server, then exit without waiting");
		ecul_log_info("	abort2						Exit immediately without sending quit");
		ecul_log_info("");
		ecul_log_info("Description:");
		ecul_log_info("	Creates anonymous pipes and starts a stdpipe_serv process to handle commands.");
		ecul_log_info("	When cleanup_exec_prog is provided, the server runs in a cleaned environment:");
		ecul_log_info("	- FD cleanup: Only stdin/stdout/stderr and the two pipe FDs are kept");
		ecul_log_info("	- Environment cleanup: Only HOME and USER environment variables are preserved");
		ecul_log_info("");
		ecul_log_info("Examples:");
		ecul_log_info("	" + program_name + " test													# Run test mode with defaults");
		ecul_log_info("	" + program_name + " demo demo1										# Run demo with get_dynamic_global_properties");
		ecul_log_info("	" + program_name + " demo gdgp										# Same as demo1");
		ecul_log_info("	" + program_name + " cli													# Interactive CLI mode");
		ecul_log_info("	" + program_name + " test \"\" capture							# Test mode with captured child output");
		ecul_log_info("	" + program_name + " cli \"\" hide ./stdpipe_serv		# CLI mode with hidden child output");
		ecul_log_info("	" + program_name + " demo demo1 -- --extra-arg		# Demo mode with extra args after '--'");
		ecul_log_info("");
}




namespace n_nodes_invite {

struct ParsedChainsysInvite {
    std::string chain_subnet;
    std::string ip_net_ip;
    std::string chainid;
    std::time_t genesis_timestamp{};
    bool valid;

    // Default ctor: invalid object
    ParsedChainsysInvite() 
        : chain_subnet(), ip_net_ip(), chainid(), genesis_timestamp(0), valid(false) {}	
};

std::ostream& operator<<(std::ostream &stre, const ParsedChainsysInvite &obj) {
	if (! obj.valid) return stre << "(invalid invite)";
	return stre << "[" << obj.chain_subnet << ',' << obj.ip_net_ip << ',' << obj.chainid << ',' << obj.genesis_timestamp << "]";
}

ParsedChainsysInvite parse_chainsys_invite(const std::string& input, std::string& out_msg);

ParsedChainsysInvite parse_any_invite() {
    // listAbstractSockets
	using ecul::mkstr;
	const std::string fn_socklist("/proc/net/unix");
    std::ifstream procFile(fn_socklist);
    if (!procFile.is_open()) throw ecul_erro_runtime(mkstr()<<"Failed to open ["<<fn_socklist<<"]");

    std::string line;
    if (!std::getline(procFile, line)) throw ecul_erro_runtime(mkstr()<<"Failed to read header ["<<fn_socklist<<"]");
	size_t cnt_abstr = 0; // how many _abstract_ sockets we so far considered from that file

    while (true) { // iterate all unix sockets in that file
        if (!std::getline(procFile, line)) {
            if (procFile.eof()) break; // normal end
			throw ecul_erro_runtime(mkstr()<<"Error on reading next line from ["<<fn_socklist<<"]");
        }

        std::istringstream iss(line);
        std::string field;
        // Skip first 7 fields (columns)
        for (int i = 0; i < 7; ++i) {
            if (!(iss >> field)) {
                // Malformed line, skip safely
                field.clear();
                break;
            }
        }

        std::string path;
        if (!(iss >> path)) {
            // No path field, skip safely
            continue;
        }

        // Abstract sockets are shown with leading '@', on linux
        if (!path.empty() && path[0] == '@') {
			 // we found an abstract socket.
			++cnt_abstr;
			std::string msg;
			ParsedChainsysInvite invite = parse_chainsys_invite(path, msg);
			ecul_info(ecul::mkstr()<<"Not invite abstr socket: [" << path << "] because [" << msg << "]");
			if (invite.valid) { // if that abstr.socet looks like our invite
				ecul_info(mkstr() << "Found nodesys-invite: " << invite << " from abstract socket path [" << path << "]");
				return invite; // ok return it
			}
        }
    }
	throw ecul_erro_runtime(mkstr() << "Can not find any invite, after checking " << cnt_abstr << " abstract sockets from " << fn_socklist);
}
 
ParsedChainsysInvite parse_chainsys_invite(const std::string& input, std::string& out_msg) {
    const std::string prefix = "@bcNodes/p2e/1:";
    if (input.size() <= prefix.size()) {
        out_msg = "Input too short";
        return ParsedChainsysInvite();
    }
    if (input.rfind(prefix, 0) != 0) {
        out_msg = "Missing or incorrect prefix";
        return ParsedChainsysInvite();
    }

    std::string rest = input.substr(prefix.size());
    if (rest.empty()) {
        out_msg = "No data after prefix";
        return ParsedChainsysInvite();
    }

    // Split by underscores
    size_t firstUnd = rest.find('_');
    if (firstUnd == std::string::npos || firstUnd == 0) {
        out_msg = "First underscore missing or misplaced";
        return ParsedChainsysInvite();
    }

    size_t secondUnd = rest.find('_', firstUnd + 1);
    if (secondUnd == std::string::npos || secondUnd <= firstUnd + 1) {
        out_msg = "Second underscore missing or misplaced";
        return ParsedChainsysInvite();
    }

    size_t thirdUnd = rest.find('_', secondUnd + 1);
    if (thirdUnd == std::string::npos || thirdUnd <= secondUnd + 1) {
        out_msg = "Third underscore missing or misplaced";
        return ParsedChainsysInvite();
    }

    ParsedChainsysInvite result;

    result.chain_subnet = rest.substr(0, firstUnd);
    result.ip_net_ip    = rest.substr(firstUnd + 1, secondUnd - firstUnd - 1);
    result.chainid      = rest.substr(secondUnd + 1, thirdUnd - secondUnd - 1);
    std::string tsStr   = rest.substr(thirdUnd + 1);

    if (result.chain_subnet.empty()) {
        out_msg = "Chain subnet is empty";
        return ParsedChainsysInvite();
    }
    if (result.ip_net_ip.empty()) {
        out_msg = "IP/net string is empty";
        return ParsedChainsysInvite();
    }
    if (result.chainid.empty()) {
        out_msg = "Chain ID is empty";
        return ParsedChainsysInvite();
    }
    if (tsStr.empty()) {
        out_msg = "Timestamp is empty";
        return ParsedChainsysInvite();
    }

    // Parse timestamp safely with istringstream
    std::istringstream iss(tsStr);
    long long ts;
    if (!(iss >> ts)) {
        out_msg = "Timestamp is not a valid integer";
        return ParsedChainsysInvite();
    }
    char leftover;
    if (iss >> leftover) {
        out_msg = "Extra characters after timestamp";
        return ParsedChainsysInvite();
    }
    if (ts < 0 || ts > std::numeric_limits<std::time_t>::max()) {
        out_msg = "Timestamp out of range";
        return ParsedChainsysInvite();
    }

    result.genesis_timestamp = static_cast<std::time_t>(ts);
    result.valid = true;
    out_msg.clear(); // no error
    return result;
}

} // namespace

/**
 * StdPipe Backend Controller
 *
 * Usage: ./stdpipe_back <mode> [submode] [server_path] [cleanup_exec_prog]
 *
 * Modes:
 * - test: Run automated ping/quit test (original behavior)
 * - demo: Same as test mode
 * - cli: Interactive command-line interface
 *
 * When cleanup_exec_prog is provided, the program executes:
 * ./clean_exec --run --clean-fd-except 0,1,2,<cmd_fd>,<resp_fd> --clean-env-except HOME,USER ./stdpipe_serv <cmd_fd> <resp_fd>
 *
 * This ensures:
 * - FD cleanup: Only stdin/stdout/stderr and the two pipe FDs are kept
 * - Environment cleanup: Only HOME and USER environment variables are preserved
 * - Proper isolation: The server runs in a cleaned environment
 */
int main(int argc, char* argv[]) {
		// Initialize ECUL colors for thread-safe logging
		ecul::init_colors();

		// Configure default ECUL logging settings
		auto& log_settings = ecul::get_log_settings();
		log_settings.set_date_format(ecul::DateFormat::no_date);
		log_settings.set_time_format(ecul::TimeFormat::short_time);
		log_settings.set_runtime_format(ecul::RuntimeFormat::ms);
		log_settings.set_program_name_format(ecul::ProgramNameFormat::prefer_bin);
		log_settings.set_line_width(4);
		log_settings.set_spacing_format(ecul::SpacingFormat::normal);

		// Configure program icon - light-blue background, white foreground
		log_settings.set_program_icon(" BACKEND");
		log_settings.set_program_icon_usecolor(true);
		log_settings.set_program_icon_fg(static_cast<int>(ecul::Color::White));
		log_settings.set_program_icon_bg(static_cast<int>(ecul::Color::LightBlue));
		log_settings.set_program_icon_show(true);

		try {
				// First convert argv to safe vector
				std::vector<std::string> argvect;
				for (int i = 0; i < argc; ++i) {
						if (argv[i] == nullptr) {
								throw ecul_stop("Null argv element at index " + std::to_string(i));
						}
						argvect.push_back(std::string(argv[i]));
				}
				ecul_info((std::ostringstream()<<"Number of arguments: "<<argvect.size()<<".").str());
				for (const auto & one : argvect) {
					ecul_info((std::ostringstream()<<"argument [" << one << "]").str());
				}

				// Parse arguments - look for "--" separator
				std::vector<std::string> main_args;
				std::vector<std::string> command_args;

				// Find first "--" and split arguments
				size_t separator_pos = argvect.size();	// Default to end if no "--" found
				for (size_t i = 0; i < argvect.size(); ++i) {
						if (argvect[i] == "--") {
								separator_pos = i;
								break;
						}
				}

				// Split arguments around "--"
				for (size_t i = 0; i < separator_pos; ++i) {
						main_args.push_back(argvect[i]);
				}
				for (size_t i = separator_pos + 1; i < argvect.size(); ++i) {
						command_args.push_back(argvect[i]);
				}

				// Check for --help
				if (main_args.size() > 1 && main_args.at(1) == "--help") {
						print_usage(main_args.at(0));
						return 0;
				}

				// Check minimum arguments
				if (main_args.size() < 2) {
						ecul_log_erro("Error: Missing required 'mode' argument");
						print_usage(main_args.at(0));
						return 1;
				}

				ecul_log_info("StdPipe Backend Controller starting...");

				// Parse new argument structure: <mode> [submode] [stdouterr] [server_path] [cleanup_exec_prog]
				std::string mode = main_args.at(1);
				std::string submode = "";
				std::string stdouterr_str = "direct";
				std::string server_path = "./stdpipe_serv";
				std::string cleanup_exec_prog = "";

				// Parse optional arguments
				if (main_args.size() > 2) {
						submode = main_args.at(2);
				}
				if (main_args.size() > 3) {
						stdouterr_str = main_args.at(3);
				}
				if (main_args.size() > 4) {
						server_path = main_args.at(4);
				}
				if (main_args.size() > 5) {
						cleanup_exec_prog = main_args.at(5);
				}

				// Store command_args for future use in command execution
				if (!command_args.empty()) {
						std::string args_str = "Command arguments after '--':";
						for (const auto& arg : command_args) {
								args_str += " '" + arg + "'";
						}
						ecul_log_info(args_str);
				}

				// Parse stdouterr mode
				StdOutErrMode stdouterr_mode = StdOutErrMode::OutErrModeDirect;
				if (stdouterr_str == "hide") {
						stdouterr_mode = StdOutErrMode::OutErrModeHide;
				} else if (stdouterr_str == "capture") {
						stdouterr_mode = StdOutErrMode::OutErrModeCapture;
				} else if (stdouterr_str == "direct") {
						stdouterr_mode = StdOutErrMode::OutErrModeDirect;
				} else {
						ecul_log_erro("Error: Invalid stdouterr mode '" + stdouterr_str + "'. Must be 'direct', 'hide', or 'capture'");
						print_usage(argvect.at(0));
						return 1;
				}

				// Validate mode
				if (mode != "test" && mode != "demo" && mode != "cli") {
						ecul_log_erro("Error: Invalid mode '" + mode + "'. Must be 'test', 'demo', or 'cli'");
						print_usage(argvect.at(0));
						return 1;
				}

				// Validate server path is not empty
				if (server_path.empty()) {
						ecul_log_erro("Error: Server path cannot be empty");
						print_usage(argvect.at(0));
						return 1;
				}

				ecul_log_info("Mode: " + mode + ", Submode: '" + submode + "', StdOutErr: " + stdouterr_str);
				ecul_log_info("Server path: " + server_path);
				if (!cleanup_exec_prog.empty()) {
						ecul_log_info("Cleanup exec: " + cleanup_exec_prog);
				}

				// Determine actual server path and arguments for demo modes
				std::string actual_server_path = server_path;
				std::vector<std::string> server_args;

				n_nodes_invite::ParsedChainsysInvite invite = n_nodes_invite::parse_any_invite();
				ecul_log_info((std::ostringstream()<<"Got chainsys-invite: " << invite).str());

				if (mode == "demo" && (submode == "demo1" || submode == "gdgp")) {
						// Use cli_wallet for demo modes that need get_dynamic_global_properties
						actual_server_path = "/home/joe/work/pay2exchange-core/use/programs/cli_wallet/cli_wallet";
						server_args = {
						// RUNTIME change this runtime. TODO fixme FIXME
								ecul::mkstr()<<"--server-rpc-endpoint=ws://"<<invite.ip_net_ip<<":1025", //"--server-rpc-endpoint=ws://127.0.0.99:1025",
								"--chain-id", invite.chainid,
								"--mutelog"  // Use --mutelog instead of --daemon to suppress logging but still enable pipe handling
						};
						// Note: --cmd-pipe XXX,YYY will be added dynamically with actual FD numbers
				}

				// Create controller and dispatch based on mode
				// PLACE: formed the args to send to child.
				ecul_info((std::ostringstream{} <<"Will start child with nubmer of args: " << server_args.size()).str());
				StdPipeController controller(actual_server_path, cleanup_exec_prog, stdouterr_mode, mode, server_args);

				if (mode == "test") {
						controller.run_test();
				} else if (mode == "demo") {
						// Check submodes for demo
						if (submode == "demo1" || submode == "gdgp") {
								controller.run_demo_gdgp();
						} else {
								controller.run_test(); // Default demo mode acts the same as test mode
						}
				} else if (mode == "cli") {
						controller.run_cli_mode();
				}

				ecul_log_info("StdPipe Backend Controller completed successfully");
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
// Test change

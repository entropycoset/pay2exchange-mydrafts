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
#include "libvalidcolor/libvalidcolor.hpp"
#include "libcmdformat/libcmdformat.hpp"
#include "libstdpipeutil/libstdpipeutil.hpp"

namespace bp = boost::process;


enum class StdOutErrMode {
    OutErrModeHide,     // Redirect stdout/stderr to /dev/null
    OutErrModeCapture,  // Capture stdout/stderr via pipes
    OutErrModeDirect    // Current behavior - inherit parent's stdout/stderr
};

// Use libcmdformat's CmdFormat enum
using libcmdformat::CmdFormat;

class my_fd {
private:
    int m_fd;    // my file descriptor, e.g. from open or one out of pipe()
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
                std::cerr << "Warning: close() failed for FD " << m_fd << ": " << std::strerror(errno) << "\n";
            }
            m_owned = false;
            m_fd = -1;
        }
    }
    
    bool is_open() const { return m_fd>=0; }

    /// Get the file descriptor, it will be valid FD (otherwise we throw)
    int get_fd() const { if (!is_open()) throw std::runtime_error("Tried to use invalid/closed FD"); return m_fd; }
    
    // Set the file descriptor and ownership. The _fd must be valid (>=-1) otherwise throws and leave object unchanged
    void set_fd(int _fd, bool _owned) {
        if (!(_fd>=0)) throw std::runtime_error("Invalid fd being set");
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
        m_read.set_fd(pipe_fds[0], true);   // Read end
        m_write.set_fd(pipe_fds[1], true);  // Write end
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
    my_pipe cmd_pipe;         // Command pipe (parent writes, child reads)
    my_pipe resp_pipe;        // Response pipe (child writes, parent reads)
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
            std::cerr << "Warning: operation took long " << operation_name << " - " << seconds << " seconds\n";
        }
        
        return result;
    }

public:
    StdPipeController(const std::string& server_path, const std::string& cleanup_exec_prog = "", StdOutErrMode stdouterr_mode = StdOutErrMode::OutErrModeDirect, const std::string& mode = "", const std::vector<std::string>& server_args = {})
        : m_stdouterr_mode(stdouterr_mode) {
        
        // Always use v1lenend format now that both client and server support it
        m_cmdformat = CmdFormat::cmdformat_v1lenend;
        if (server_path.empty()) {
            throw std::runtime_error("Server path cannot be empty");
        }
        
        std::cerr << "StdPipeController: Starting server process: " << server_path << "\n";
        if (!cleanup_exec_prog.empty()) {
            std::cerr << "StdPipeController: Using cleanup_exec: " << cleanup_exec_prog << "\n";
        } else {
            std::cerr << "StdPipeController: Warning: No cleanup_exec program specified - server will run in current environment\n";
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
                
                std::cerr << "StdPipeController: Created secure anonymous pipes for stdout/stderr capture\n";
            }
            
            std::cerr << "StdPipeController: Created pipes - cmd_pipe[" << cmd_pipe.side_read().get_fd() << "," << cmd_pipe.side_write().get_fd()
                      << "], resp_pipe[" << resp_pipe.side_read().get_fd() << "," << resp_pipe.side_write().get_fd() << "]\n";
            
            // Convert FD numbers to strings for passing as arguments
            std::string cmd_fd_str = std::to_string(cmd_pipe.side_read().get_fd());
            std::string resp_fd_str = std::to_string(resp_pipe.side_write().get_fd());
            
            std::cerr << "StdPipeController: Will pass FDs " << cmd_fd_str << " and " << resp_fd_str << " to child process\n";
            
            // Prepare arguments vector
            std::vector<std::string> all_args;
            
            // Add server-specific arguments first (for cli_wallet)
            for (const auto& arg : server_args) {
                all_args.push_back(arg);
            }
            
            // Add cmd-pipe argument for cli_wallet or FD args for stdpipe_serv
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
            std::cerr << "StdPipeController: Starting with args:";
            for (const auto& arg : all_args) {
                std::cerr << " '" << arg << "'";
            }
            std::cerr << "\n";
            
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
                            throw std::runtime_error("Failed to fork child process");
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
                
                std::cerr << "StdPipeController: Running via cleanup_exec with clean-fd-except=" << clean_fd_except
                          << " and clean-env-except=" << clean_env_except << "\n";
                
                switch (m_stdouterr_mode) {
                    case StdOutErrMode::OutErrModeHide:
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
                            throw std::runtime_error("Failed to fork child process");
                        }
                        break;
                    }
                    case StdOutErrMode::OutErrModeDirect:
                    default:
                        server_process = bp::child(
                            cleanup_exec_prog,
                            "--run",
                            "--clean-fd-except", clean_fd_except,
                            "--clean-env-except", clean_env_except,
                            server_path,
                            cmd_fd_str,
                            resp_fd_str,
                            bp::std_in.close()
                        );
                        break;
                }
            }
            
            // Close the child's ends in parent
            cmd_pipe.side_read().close();   // Child uses this for reading
            resp_pipe.side_write().close(); // Child uses this for writing
            
            // Close write ends of capture pipes in parent (child writes to them)
            if (m_stdouterr_mode == StdOutErrMode::OutErrModeCapture) {
                child_stdout_pipe.side_write().close();
                child_stderr_pipe.side_write().close();
            }
            
            std::cerr << "StdPipeController: Created anonymous pipes - cmd input FD " << cmd_fd_str
                      << ", response output FD " << resp_fd_str << "\n";
            
            // Give the child a moment to start and check if it's still running
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (!server_process.running()) {
                throw std::runtime_error("Failed to start server process");
            }
            
            std::cerr << "StdPipeController: Server process started successfully\n";
            
        } catch (const std::exception& e) {
            std::cerr << "Error starting server process: " << e.what() << "\n";
            throw;
        }
    }

    ~StdPipeController() {
        if (server_process.running()) {
            std::cerr << "StdPipeController: Terminating server process\n";
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
        // White text on dark-blue background for commands we send
        std::cerr << colordetect::colorstr("StdPipeController: Sending command: " + command,
                                          colordetect::Color::White, colordetect::Color::Blue) << "\n";
        
        timed_pipe_operation("sending command", [&]() {
            // Add timeout using select() for write operation
            fd_set write_fds;
            FD_ZERO(&write_fds);
            
            int fd = cmd_pipe.side_write().get_fd();
            if (fd < 0 || fd >= FD_SETSIZE) {
                throw std::runtime_error("Invalid file descriptor for write select(): " + std::to_string(fd));
            }
            
            FD_SET(fd, &write_fds);
            
            struct timeval timeout_val;
            timeout_val.tv_sec = max_timeout.count();
            timeout_val.tv_usec = 0;
            
            int select_result = stdpipeutil::check_syscall(select(fd + 1, nullptr, &write_fds, nullptr, &timeout_val), "select write");
            
            if (select_result == 0) {
                throw std::runtime_error("Timeout writing command to pipe (" + std::to_string(max_timeout.count()) + " seconds)");
            }
            
            if (!FD_ISSET(fd, &write_fds)) {
                throw std::runtime_error("select() returned but FD is not ready for writing");
            }
            
            // Use libcmdformat to encode the command
            std::string formatted_command = libcmdformat::encode_command(command, m_cmdformat);
            
            // Show raw encoded command being sent
            std::cerr << colordetect::colorstr("StdPipeController: Sending RAW: " + formatted_command,
                                              colordetect::Color::LightCyan, colordetect::Color::Black) << "\n";
            
            ssize_t bytes_written = stdpipeutil::check_syscall(write(fd, formatted_command.c_str(), formatted_command.length()), "write");
            
            if (bytes_written != static_cast<ssize_t>(formatted_command.length())) {
                throw std::runtime_error("Partial write to command pipe: " + std::to_string(bytes_written) +
                                        " of " + std::to_string(formatted_command.length()) + " bytes written");
            }
            
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
                throw std::runtime_error("Invalid file descriptor for select(): " + std::to_string(fd));
            }
            
            FD_SET(fd, &read_fds);
            
            struct timeval timeout_val;
            timeout_val.tv_sec = max_timeout.count();
            timeout_val.tv_usec = 0;
            
            int select_result = stdpipeutil::check_syscall(select(fd + 1, &read_fds, nullptr, nullptr, &timeout_val), "select");
            
            if (select_result == 0) {
                throw std::runtime_error("Timeout waiting for server response (" + std::to_string(max_timeout.count()) + " seconds)");
            }
            
            // Verify the FD is actually ready for reading
            if (!FD_ISSET(fd, &read_fds)) {
                throw std::runtime_error("select() returned but FD is not ready for reading");
            }
            
            // First read the raw data to show what we received
            char buffer[1024];
            ssize_t bytes_read = stdpipeutil::check_syscall(read(fd, buffer, sizeof(buffer) - 1), "read");
            
            if (bytes_read == 0) {
                throw std::runtime_error("Server closed response pipe");
            }
            
            if (bytes_read < 0 || bytes_read >= static_cast<ssize_t>(sizeof(buffer))) {
                throw std::runtime_error("Invalid bytes_read from read(): " + std::to_string(bytes_read));
            }
            
            buffer[bytes_read] = '\0';
            std::string raw_response = buffer;
            
            // Show raw received data
            std::cerr << colordetect::colorstr("StdPipeController: Received RAW: " + raw_response,
                                              colordetect::Color::LightYellow, colordetect::Color::Black) << "\n";
            
            // Now decode the raw response using libcmdformat
            std::istringstream raw_stream(raw_response);
            std::string decoded_response = libcmdformat::decode_command(raw_stream, m_cmdformat);
            
            // Bright white on black background for decoded responses
            std::cerr << colordetect::colorstr("StdPipeController: Decoded response: " + decoded_response,
                                              colordetect::Color::LightWhite, colordetect::Color::Black) << "\n";
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
            // Light green for stdout
            std::cout << colordetect::colorstr("[CHILD STDOUT] ", colordetect::Color::LightGreen)
                      << colordetect::colorstr(accumulated_stdout, colordetect::Color::LightGreen);
            if (accumulated_stdout.back() != '\n') {
                // Reset color before newline
                std::cout << colordetect::colorstr("", colordetect::Color::Default) << '\n';
            }
            accumulated_stdout.clear();
        }
        if (!accumulated_stderr.empty()) {
            // Light red for stderr
            std::cout << colordetect::colorstr("[CHILD STDERR] ", colordetect::Color::LightRed)
                      << colordetect::colorstr(accumulated_stderr, colordetect::Color::LightRed);
            if (accumulated_stderr.back() != '\n') {
                // Reset color before newline
                std::cout << colordetect::colorstr("", colordetect::Color::Default) << '\n';
            }
            accumulated_stderr.clear();
        }
    }

    void run_cli_mode() {
        std::cerr << "StdPipeController: Starting CLI interactive mode\n";
        std::cout << "Interactive CLI mode. Type 'quit', 'abort', or 'abort2' to exit.\n";
        
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
                    std::cout << "\nEOF reached, sending quit and exiting.\n";
                    std::string response = send_command_and_read_reply("quit");
                    std::cout << "Server response: " << response << std::endl;
                    break;
                }
                
                if (line == "quit") {
                    // Send quit, wait for response, then exit
                    std::string response = send_command_and_read_reply("quit");
                    std::cout << "Server response: " << response << std::endl;
                    break;
                } else if (line == "abort") {
                    // Send quit, then exit without waiting for response
                    send_command("quit");
                    std::cout << "Sent quit command, exiting without waiting for response.\n";
                    break;
                } else if (line == "abort2") {
                    // Exit immediately without sending quit - terminate server
                    std::cout << "Exiting immediately without sending quit.\n";
                    if (server_process.running()) {
                        server_process.terminate();
                    }
                    break;
                } else {
                    // Send the command and display response
                    try {
                        std::string response = send_command_and_read_reply(line);
                        std::cout << "Server response: " << response << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "Error communicating with server: " << e.what() << std::endl;
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
            
            std::cerr << "CLI mode completed\n";
            
        } catch (const std::exception& e) {
            std::cerr << "CLI mode error: " << e.what() << "\n";
            throw;
        }
    }

    void run_test() {
        std::cerr << "StdPipeController: Starting communication test\n";
        
        try {
            // Test 1: Send ping, expect pong
            std::string response1 = send_command_and_read_reply("ping");
            if (response1 != "pong") {
                throw std::runtime_error("Expected 'pong' but got: '" + response1 + "'");
            }
            std::cerr << "✓ Ping test passed\n";
            
            // Display any captured child output after ping test
            handle_child();
            display_and_clear_captured();
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Test 2: Send quit
            std::string response2 = send_command_and_read_reply("quit");
            if (response2 != "goodbye") {
                throw std::runtime_error("Expected 'goodbye' but got: '" + response2 + "'");
            }
            std::cerr << "✓ Quit test passed\n";
            
            // Display any final captured child output
            handle_child();
            display_and_clear_captured();
            
            // Close our end of the pipes
            cmd_pipe.side_write().close();
            resp_pipe.side_read().close();
            
            // Wait for server to exit
            server_process.wait();
            
            if (server_process.exit_code() != 0) {
                throw std::runtime_error("Server process exited with code: " +
                                       std::to_string(server_process.exit_code()));
            }
            
            std::cerr << "✓ All tests passed successfully\n";
            
        } catch (const std::exception& e) {
            std::cerr << "✗ Test failed: " << e.what() << "\n";
            throw;
        }
    }

    void run_demo_gdgp() {
        std::cerr << "StdPipeController: Starting demo mode - get_dynamic_global_properties\n";
        
        try {
            // Set longer timeout for cli_wallet initialization and RPC connection
            set_timeouts(30); // 30 seconds timeout for cli_wallet
            
            // Give cli_wallet some time to connect to RPC endpoint
            std::cerr << "StdPipeController: Waiting for cli_wallet to initialize and connect to RPC...\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Send get_dynamic_global_properties command
            std::cerr << "StdPipeController: Sending get_dynamic_global_properties command...\n";
            std::string response = send_command_and_read_reply("get_dynamic_global_properties");
            
            // Parse JSON response
            nlohmann::json json_response;
            try {
                json_response = nlohmann::json::parse(response);
            } catch (const nlohmann::json::parse_error& e) {
                throw std::runtime_error("Failed to parse JSON response: " + std::string(e.what()));
            }
            
            // Print nicely formatted JSON
            std::cout << "JSON Response (formatted):\n";
            std::cout << json_response.dump(4) << std::endl;
            
            // Extract the 3 required values
            try {
                auto head_block_number = json_response["head_block_number"].get<int>();
                auto head_block_id = json_response["head_block_id"].get<std::string>();
                auto time = json_response["time"].get<std::string>();
                
                std::cout << "\nExtracted values:\n";
                std::cout << "head_block_number: " << head_block_number << std::endl;
                std::cout << "head_block_id: " << head_block_id << std::endl;
                std::cout << "time: " << time << std::endl;
                
            } catch (const nlohmann::json::exception& e) {
                throw std::runtime_error("Failed to extract required JSON fields: " + std::string(e.what()));
            }
            
            // Send quit command
            std::string quit_response = send_command_and_read_reply("quit");
            std::cerr << "✓ Demo completed, server response to quit: " << quit_response << std::endl;
            
            // Display any final captured child output
            handle_child();
            display_and_clear_captured();
            
            // Close our end of the pipes
            cmd_pipe.side_write().close();
            resp_pipe.side_read().close();
            
            // Wait for server to exit
            server_process.wait();
            
            if (server_process.exit_code() != 0) {
                throw std::runtime_error("Server process exited with code: " +
                                       std::to_string(server_process.exit_code()));
            }
            
            std::cerr << "✓ Demo mode completed successfully\n";
            
        } catch (const std::exception& e) {
            std::cerr << "✗ Demo mode failed: " << e.what() << "\n";
            throw;
        }
    }
};

void print_usage(const std::string& program_name) {
    std::cout << "StdPipe Backend Controller\n\n";
    std::cout << "Usage: " << program_name << " <mode> [submode] [stdouterr] [server_path] [cleanup_exec_prog]\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  mode              Operation mode: 'test', 'demo', or 'cli'\n";
    std::cout << "  submode           Optional submode string (default: empty)\n";
    std::cout << "  stdouterr         Child stdout/stderr handling: 'direct', 'hide', or 'capture' (default: direct)\n";
    std::cout << "  server_path       Path to stdpipe_serv executable (default: ./stdpipe_serv)\n";
    std::cout << "  cleanup_exec_prog Optional path to clean_exec program for environment cleanup\n\n";
    std::cout << "Modes:\n";
    std::cout << "  test              Run automated ping/quit test (original behavior)\n";
    std::cout << "  demo              Demo mode with submodes:\n";
    std::cout << "                    - demo1/gdgp: Send get_dynamic_global_properties, parse JSON, extract values\n";
    std::cout << "                    - (empty): Same as test mode\n";
    std::cout << "  cli               Interactive command-line interface\n\n";
    std::cout << "StdOutErr Modes:\n";
    std::cout << "  direct            Child output goes directly to terminal (default)\n";
    std::cout << "  hide              Child output is redirected to /dev/null (hidden)\n";
    std::cout << "  capture           Child output is captured and shown before CLI prompts\n\n";
    std::cout << "CLI Mode Commands:\n";
    std::cout << "  <any text>        Send command to server and display response\n";
    std::cout << "  quit              Send quit to server, wait for response, then exit\n";
    std::cout << "  abort             Send quit to server, then exit without waiting\n";
    std::cout << "  abort2            Exit immediately without sending quit\n\n";
    std::cout << "Description:\n";
    std::cout << "  Creates anonymous pipes and starts a stdpipe_serv process to handle commands.\n";
    std::cout << "  When cleanup_exec_prog is provided, the server runs in a cleaned environment:\n";
    std::cout << "  - FD cleanup: Only stdin/stdout/stderr and the two pipe FDs are kept\n";
    std::cout << "  - Environment cleanup: Only HOME and USER environment variables are preserved\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " test                         # Run test mode with defaults\n";
    std::cout << "  " << program_name << " demo demo1                   # Run demo with get_dynamic_global_properties\n";
    std::cout << "  " << program_name << " demo gdgp                    # Same as demo1\n";
    std::cout << "  " << program_name << " cli                          # Interactive CLI mode\n";
    std::cout << "  " << program_name << " test \"\" capture              # Test mode with captured child output\n";
    std::cout << "  " << program_name << " cli \"\" hide ./stdpipe_serv   # CLI mode with hidden child output\n";
    std::cout << "  " << program_name << " demo demo1 -- --extra-arg    # Demo mode with extra args after '--'\n\n";
}

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
    try {
        // First convert argv to safe vector
        std::vector<std::string> argvect;
        for (int i = 0; i < argc; ++i) {
            if (argv[i] == nullptr) {
                throw std::runtime_error("Null argv element at index " + std::to_string(i));
            }
            argvect.push_back(std::string(argv[i]));
        }
        
        // Parse arguments - look for "--" separator
        std::vector<std::string> main_args;
        std::vector<std::string> command_args;
        
        // Find first "--" and split arguments
        size_t separator_pos = argvect.size();  // Default to end if no "--" found
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
            std::cerr << "Error: Missing required 'mode' argument\n\n";
            print_usage(main_args.at(0));
            return 1;
        }
        
        std::cerr << "StdPipe Backend Controller starting...\n";
        
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
            std::cerr << "Command arguments after '--':";
            for (const auto& arg : command_args) {
                std::cerr << " '" << arg << "'";
            }
            std::cerr << "\n";
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
            std::cerr << "Error: Invalid stdouterr mode '" << stdouterr_str << "'. Must be 'direct', 'hide', or 'capture'\n\n";
            print_usage(argvect.at(0));
            return 1;
        }
        
        // Validate mode
        if (mode != "test" && mode != "demo" && mode != "cli") {
            std::cerr << "Error: Invalid mode '" << mode << "'. Must be 'test', 'demo', or 'cli'\n\n";
            print_usage(argvect.at(0));
            return 1;
        }
        
        // Validate server path is not empty
        if (server_path.empty()) {
            std::cerr << "Error: Server path cannot be empty\n\n";
            print_usage(argvect.at(0));
            return 1;
        }
        
        std::cerr << "Mode: " << mode << ", Submode: '" << submode << "', StdOutErr: " << stdouterr_str << "\n";
        std::cerr << "Server path: " << server_path << "\n";
        if (!cleanup_exec_prog.empty()) {
            std::cerr << "Cleanup exec: " << cleanup_exec_prog << "\n";
        }
        
        // Determine actual server path and arguments for demo modes
        std::string actual_server_path = server_path;
        std::vector<std::string> server_args;
        
        if (mode == "demo" && (submode == "demo1" || submode == "gdgp")) {
            // Use cli_wallet for demo modes that need get_dynamic_global_properties
            actual_server_path = "/home/joe/work/pay2exchange-core/use/programs/cli_wallet/cli_wallet";
            server_args = {
                "--server-rpc-endpoint=ws://127.0.0.1:1025",
                "--chain-id", "08140cebb7723d4f8ffc6ce380b5dbb4c8cdd51b4e3c644d52af1918098a7643",
                "--mutelog"  // Use --mutelog instead of --daemon to suppress logging but still enable pipe handling
            };
            // Note: --cmd-pipe XXX,YYY will be added dynamically with actual FD numbers
        }
        
        // Create controller and dispatch based on mode
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
        
        std::cerr << "StdPipe Backend Controller completed successfully\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught\n";
        return 2;
    }
}

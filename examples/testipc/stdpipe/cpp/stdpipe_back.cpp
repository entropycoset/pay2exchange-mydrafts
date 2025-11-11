#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <boost/process.hpp>

namespace bp = boost::process;

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
            ::close(m_fd);
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
        if (pipe(pipe_fds) == -1) {
            throw std::runtime_error("Failed to create pipe");
        }
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
    bp::child server_process; // The stdpipe_serv process

public:
    StdPipeController(const std::string& server_path, const std::string& cleanup_exec_prog = "") {
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
            
            std::cerr << "StdPipeController: Created pipes - cmd_pipe[" << cmd_pipe.side_read().get_fd() << "," << cmd_pipe.side_write().get_fd()
                      << "], resp_pipe[" << resp_pipe.side_read().get_fd() << "," << resp_pipe.side_write().get_fd() << "]\n";
            
            // Convert FD numbers to strings for passing as arguments
            std::string cmd_fd_str = std::to_string(cmd_pipe.side_read().get_fd());
            std::string resp_fd_str = std::to_string(resp_pipe.side_write().get_fd());
            
            std::cerr << "StdPipeController: Will pass FDs " << cmd_fd_str << " and " << resp_fd_str << " to child process\n";
            
            // Start the server process - either directly or via cleanup_exec
            if (cleanup_exec_prog.empty()) {
                // Direct execution (original behavior)
                server_process = bp::child(
                    server_path,
                    cmd_fd_str,
                    resp_fd_str,
                    bp::std_in.close()
                );
            } else {
                // Execute via cleanup_exec with cleanup options
                std::string clean_fd_except = "0,1,2," + cmd_fd_str + "," + resp_fd_str;
                std::string clean_env_except = "HOME,USER";
                
                std::cerr << "StdPipeController: Running via cleanup_exec with clean-fd-except=" << clean_fd_except
                          << " and clean-env-except=" << clean_env_except << "\n";
                
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
            }
            
            // Close the child's ends in parent
            cmd_pipe.side_read().close();   // Child uses this for reading
            resp_pipe.side_write().close(); // Child uses this for writing
            
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

    void send_command(const std::string& command) {
        std::cerr << "StdPipeController: Sending command: " << command << "\n";
        std::string cmd_with_newline = command + "\n";
        ssize_t bytes_written = write(cmd_pipe.side_write().get_fd(), cmd_with_newline.c_str(), cmd_with_newline.length());
        
        if (bytes_written == -1 || bytes_written != static_cast<ssize_t>(cmd_with_newline.length())) {
            throw std::runtime_error("Failed to send command to server");
        }
    }

    std::string read_response() {
        std::string response;
        char buffer[1024];
        ssize_t bytes_read = read(resp_pipe.side_read().get_fd(), buffer, sizeof(buffer) - 1);
        
        if (bytes_read == -1) {
            throw std::runtime_error("Failed to read response from server");
        } else if (bytes_read == 0) {
            throw std::runtime_error("Server closed response pipe");
        }
        
        // Ensure bounds safety: Check negative first to avoid wraparound, then check upper bound
        if (bytes_read < 0) {
            throw std::runtime_error("Negative bytes_read from read(): " + std::to_string(bytes_read));
        }
        if (bytes_read >= static_cast<ssize_t>(sizeof(buffer))) {
            throw std::runtime_error("bytes_read exceeds buffer size: " + std::to_string(bytes_read) +
                                    " >= " + std::to_string(sizeof(buffer)));
        }
        
        buffer[bytes_read] = '\0';
        response = buffer;
        
        // Remove trailing newline if present
        if (!response.empty() && response.back() == '\n') {
            response.pop_back();
        }
        
        std::cerr << "StdPipeController: Received response: " << response << "\n";
        return response;
    }

    std::string send_command_and_read_reply(const std::string& command) {
        send_command(command);
        return read_response();
    }

    void run_cli_mode() {
        std::cerr << "StdPipeController: Starting CLI interactive mode\n";
        std::cout << "Interactive CLI mode. Type 'quit', 'abort', or 'abort2' to exit.\n";
        
        try {
            std::string line;
            while (true) {
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
                    // Exit immediately without sending quit
                    std::cout << "Exiting immediately without sending quit.\n";
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
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Test 2: Send quit
            std::string response2 = send_command_and_read_reply("quit");
            if (response2 != "goodbye") {
                throw std::runtime_error("Expected 'goodbye' but got: '" + response2 + "'");
            }
            std::cerr << "✓ Quit test passed\n";
            
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
};

void print_usage(const std::string& program_name) {
    std::cout << "StdPipe Backend Controller\n\n";
    std::cout << "Usage: " << program_name << " <mode> [submode] [server_path] [cleanup_exec_prog]\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  mode              Operation mode: 'test', 'demo', or 'cli'\n";
    std::cout << "  submode           Optional submode string (default: empty)\n";
    std::cout << "  server_path       Path to stdpipe_serv executable (default: ./stdpipe_serv)\n";
    std::cout << "  cleanup_exec_prog Optional path to clean_exec program for environment cleanup\n\n";
    std::cout << "Modes:\n";
    std::cout << "  test              Run automated ping/quit test (original behavior)\n";
    std::cout << "  demo              Same as test mode\n";
    std::cout << "  cli               Interactive command-line interface\n\n";
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
    std::cout << "  " << program_name << " cli                          # Interactive CLI mode\n";
    std::cout << "  " << program_name << " test \"\" ./stdpipe_serv       # Specify server path\n";
    std::cout << "  " << program_name << " cli \"\" ./stdpipe_serv ./clean_exec # CLI with cleanup\n\n";
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
        
        // Check for --help
        if (argvect.size() > 1 && argvect.at(1) == "--help") {
            print_usage(argvect.at(0));
            return 0;
        }
        
        // Check minimum arguments
        if (argvect.size() < 2) {
            std::cerr << "Error: Missing required 'mode' argument\n\n";
            print_usage(argvect.at(0));
            return 1;
        }
        
        std::cerr << "StdPipe Backend Controller starting...\n";
        
        // Parse new argument structure: <mode> [submode] [server_path] [cleanup_exec_prog]
        std::string mode = argvect.at(1);
        std::string submode = "";
        std::string server_path = "./stdpipe_serv";
        std::string cleanup_exec_prog = "";
        
        // Parse optional arguments
        if (argvect.size() > 2) {
            submode = argvect.at(2);
        }
        if (argvect.size() > 3) {
            server_path = argvect.at(3);
        }
        if (argvect.size() > 4) {
            cleanup_exec_prog = argvect.at(4);
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
        
        std::cerr << "Mode: " << mode << ", Submode: '" << submode << "'\n";
        std::cerr << "Server path: " << server_path << "\n";
        if (!cleanup_exec_prog.empty()) {
            std::cerr << "Cleanup exec: " << cleanup_exec_prog << "\n";
        }
        
        // Create controller and dispatch based on mode
        StdPipeController controller(server_path, cleanup_exec_prog);
        
        if (mode == "test") {
            controller.run_test();
        } else if (mode == "demo") {
            controller.run_test(); // Demo mode acts the same as test mode
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

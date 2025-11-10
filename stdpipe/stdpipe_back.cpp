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
    StdPipeController(const std::string& server_path) {
        std::cerr << "StdPipeController: Starting server process: " << server_path << "\n";
        
        try {
            // Close FDs 3 and 4 if they're open to ensure they're available
            close(3); close(4);
            
            // Create pipes using my_pipe class
            cmd_pipe.spawn();
            resp_pipe.spawn();
            
            std::cerr << "StdPipeController: Created pipes - cmd_pipe[" << cmd_pipe.side_read().get_fd() << "," << cmd_pipe.side_write().get_fd()
                      << "], resp_pipe[" << resp_pipe.side_read().get_fd() << "," << resp_pipe.side_write().get_fd() << "]\n";
                      
            std::cerr << "StdPipeController: Will duplicate cmd_pipe.side_read().get_fd()=" << cmd_pipe.side_read().get_fd() << " to FD 3 (child reads)\n";
            std::cerr << "StdPipeController: Will duplicate resp_pipe.side_write().get_fd()=" << resp_pipe.side_write().get_fd() << " to FD 4 (child writes)\n";
            
            // Manual fork for FD setup, then use boost::process to manage child
            pid_t raw_pid = fork();
            if (raw_pid == -1) {
                throw std::runtime_error("Failed to fork server process");
            }
            
            if (raw_pid == 0) {
                // Child process - set up FDs manually
                int cmd_read_fd = cmd_pipe.side_read().get_fd();
                int resp_write_fd = resp_pipe.side_write().get_fd();
                
                std::cerr << "Child: About to dup2 cmd_pipe.read_fd()=" << cmd_read_fd << " to FD 3\n";
                if (cmd_read_fd != 3) {
                    if (dup2(cmd_read_fd, 3) == -1) {
                        std::cerr << "Child: Failed to dup2 cmd_pipe read to FD 3, errno=" << errno << "\n";
                        _exit(1);
                    }
                }
                std::cerr << "Child: About to dup2 resp_pipe.write_fd()=" << resp_write_fd << " to FD 4\n";
                if (resp_write_fd != 4) {
                    if (dup2(resp_write_fd, 4) == -1) {
                        std::cerr << "Child: Failed to dup2 resp_pipe write to FD 4, errno=" << errno << "\n";
                        _exit(1);
                    }
                }
                
                // Close original pipe ends in child, but only if they're not FD 3 or 4
                if (cmd_read_fd != 3) close(cmd_read_fd);
                if (cmd_pipe.side_write().get_fd() != 3 && cmd_pipe.side_write().get_fd() != 4) close(cmd_pipe.side_write().get_fd());
                if (resp_pipe.side_read().get_fd() != 3 && resp_pipe.side_read().get_fd() != 4) close(resp_pipe.side_read().get_fd());
                if (resp_write_fd != 4) close(resp_write_fd);
                
                std::cerr << "Child: About to exec server with FD 3,4\n";
                // Execute the server
                execl(server_path.c_str(), server_path.c_str(), "3", "4", nullptr);
                std::cerr << "Child: execl failed, errno=" << errno << "\n";
                _exit(1); // execl failed
            }
            
            // Parent process - create boost::process child from existing PID
            server_process = bp::child(raw_pid);
            
            // Close the child's ends in parent
            cmd_pipe.side_read().close();   // Child uses this for reading (now FD 3)
            resp_pipe.side_write().close(); // Child uses this for writing (now FD 4)
            
            std::cerr << "StdPipeController: Created anonymous pipes - FD 3 (cmd input), FD 4 (response output)\n";
            
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
        
        buffer[bytes_read] = '\0';
        response = buffer;
        
        // Remove trailing newline if present
        if (!response.empty() && response.back() == '\n') {
            response.pop_back();
        }
        
        std::cerr << "StdPipeController: Received response: " << response << "\n";
        return response;
    }

    void run_test() {
        std::cerr << "StdPipeController: Starting communication test\n";
        
        try {
            // Test 1: Send ping, expect pong
            send_command("ping");
            std::string response1 = read_response();
            if (response1 != "pong") {
                throw std::runtime_error("Expected 'pong' but got: '" + response1 + "'");
            }
            std::cerr << "✓ Ping test passed\n";
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Test 2: Send quit
            send_command("quit");
            std::string response2 = read_response();
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

int main(int argc, char* argv[]) {
    try {
        std::cerr << "StdPipe Backend Controller starting...\n";
        
        // Determine server path - assume it's in the same directory
        std::string server_path = "./stdpipe_serv";
        if (argc > 1) {
            server_path = argv[1];
        }
        
        StdPipeController controller(server_path);
        controller.run_test();
        
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

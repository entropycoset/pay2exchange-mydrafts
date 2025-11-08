#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

class StdPipeController {
private:
    int cmd_write_fd;         // Write commands to server (parent's end)
    int resp_read_fd;         // Read responses from server (parent's end)
    pid_t server_pid;         // The stdpipe_serv process ID

public:
    StdPipeController(const std::string& server_path) {
        std::cerr << "StdPipeController: Starting server process: " << server_path << std::endl;
        
        try {
            // Close FDs 3 and 4 if they're open to ensure they're available
            close(3); close(4);
            
            // Create manual pipes for FD 3 and 4
            int cmd_pipe[2], resp_pipe[2];
            if (pipe(cmd_pipe) == -1) {
                throw std::runtime_error("Failed to create command pipe");
            }
            if (pipe(resp_pipe) == -1) {
                close(cmd_pipe[0]); close(cmd_pipe[1]);
                throw std::runtime_error("Failed to create response pipe");
            }
            
            std::cerr << "StdPipeController: Created pipes - cmd_pipe[" << cmd_pipe[0] << "," << cmd_pipe[1]
                      << "], resp_pipe[" << resp_pipe[0] << "," << resp_pipe[1] << "]" << std::endl;
                      
            std::cerr << "StdPipeController: Will duplicate cmd_pipe[0]=" << cmd_pipe[0] << " to FD 3 (child reads)" << std::endl;
            std::cerr << "StdPipeController: Will duplicate resp_pipe[1]=" << resp_pipe[1] << " to FD 4 (child writes)" << std::endl;
            
            // Store parent's ends
            cmd_write_fd = cmd_pipe[1];   // Parent writes commands here
            resp_read_fd = resp_pipe[0];  // Parent reads responses here
            
            // Fork and exec the server process
            server_pid = fork();
            if (server_pid == -1) {
                close(cmd_pipe[0]); close(cmd_pipe[1]);
                close(resp_pipe[0]); close(resp_pipe[1]);
                throw std::runtime_error("Failed to fork server process");
            }
            
            if (server_pid == 0) {
                // Child process
                // Duplicate pipes to FD 3 and 4
                std::cerr << "Child: About to dup2 cmd_pipe[0]=" << cmd_pipe[0] << " to FD 3" << std::endl;
                if (cmd_pipe[0] != 3) {
                    if (dup2(cmd_pipe[0], 3) == -1) {
                        std::cerr << "Child: Failed to dup2 cmd_pipe read to FD 3, errno=" << errno << std::endl;
                        _exit(1);
                    }
                }
                std::cerr << "Child: About to dup2 resp_pipe[1]=" << resp_pipe[1] << " to FD 4" << std::endl;
                if (resp_pipe[1] != 4) {
                    if (dup2(resp_pipe[1], 4) == -1) {
                        std::cerr << "Child: Failed to dup2 resp_pipe write to FD 4, errno=" << errno << std::endl;
                        _exit(1);
                    }
                }
                
                // Close original pipe ends in child, but only if they're not FD 3 or 4
                if (cmd_pipe[0] != 3) close(cmd_pipe[0]);
                if (cmd_pipe[1] != 3 && cmd_pipe[1] != 4) close(cmd_pipe[1]);
                if (resp_pipe[0] != 3 && resp_pipe[0] != 4) close(resp_pipe[0]);
                if (resp_pipe[1] != 4) close(resp_pipe[1]);
                
                std::cerr << "Child: About to exec server with FD 3,4" << std::endl;
                // Execute the server
                execl(server_path.c_str(), server_path.c_str(), "3", "4", nullptr);
                std::cerr << "Child: execl failed, errno=" << errno << std::endl;
                _exit(1); // execl failed
            }
            
            // Parent process
            // Close the child's ends in the parent
            close(cmd_pipe[0]);  // Child uses this for reading (now FD 3)
            close(resp_pipe[1]); // Child uses this for writing (now FD 4)
            
            std::cerr << "StdPipeController: Created anonymous pipes - FD 3 (cmd input), FD 4 (response output)" << std::endl;
            
            // Give the child a moment to start and check if it's still running
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int status;
            pid_t result = waitpid(server_pid, &status, WNOHANG);
            if (result != 0) {
                if (result == server_pid) {
                    if (WIFEXITED(status)) {
                        throw std::runtime_error("Server process exited immediately with code: " + std::to_string(WEXITSTATUS(status)));
                    } else {
                        throw std::runtime_error("Server process terminated abnormally");
                    }
                } else {
                    throw std::runtime_error("Failed to check server process status");
                }
            }
            
            std::cerr << "StdPipeController: Server process started successfully" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error starting server process: " << e.what() << std::endl;
            throw;
        }
    }

    ~StdPipeController() {
        if (server_pid > 0) {
            std::cerr << "StdPipeController: Terminating server process" << std::endl;
            kill(server_pid, SIGTERM);
            waitpid(server_pid, nullptr, 0);
        }
        if (cmd_write_fd >= 0) close(cmd_write_fd);
        if (resp_read_fd >= 0) close(resp_read_fd);
    }

    void send_command(const std::string& command) {
        std::cerr << "StdPipeController: Sending command: " << command << std::endl;
        std::string cmd_with_newline = command + "\n";
        ssize_t bytes_written = write(cmd_write_fd, cmd_with_newline.c_str(), cmd_with_newline.length());
        
        if (bytes_written == -1 || bytes_written != static_cast<ssize_t>(cmd_with_newline.length())) {
            throw std::runtime_error("Failed to send command to server");
        }
    }

    std::string read_response() {
        std::string response;
        char buffer[1024];
        ssize_t bytes_read = read(resp_read_fd, buffer, sizeof(buffer) - 1);
        
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
        
        std::cerr << "StdPipeController: Received response: " << response << std::endl;
        return response;
    }

    void run_test() {
        std::cerr << "StdPipeController: Starting communication test" << std::endl;
        
        try {
            // Test 1: Send ping, expect pong
            send_command("ping");
            std::string response1 = read_response();
            if (response1 != "pong") {
                throw std::runtime_error("Expected 'pong' but got: '" + response1 + "'");
            }
            std::cerr << "✓ Ping test passed" << std::endl;
            
            // Small delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Test 2: Send quit
            send_command("quit");
            std::string response2 = read_response();
            if (response2 != "goodbye") {
                throw std::runtime_error("Expected 'goodbye' but got: '" + response2 + "'");
            }
            std::cerr << "✓ Quit test passed" << std::endl;
            
            // Close our end of the pipes
            close(cmd_write_fd);
            close(resp_read_fd);
            cmd_write_fd = -1;
            resp_read_fd = -1;
            
            // Wait for server to exit
            int status;
            waitpid(server_pid, &status, 0);
            
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                throw std::runtime_error("Server process exited with error");
            }
            
            std::cerr << "✓ All tests passed successfully" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "✗ Test failed: " << e.what() << std::endl;
            throw;
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        std::cerr << "StdPipe Backend Controller starting..." << std::endl;
        
        // Determine server path - assume it's in the same directory
        std::string server_path = "./stdpipe_serv";
        if (argc > 1) {
            server_path = argv[1];
        }
        
        StdPipeController controller(server_path);
        controller.run_test();
        
        std::cerr << "StdPipe Backend Controller completed successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
        return 2;
    }
}

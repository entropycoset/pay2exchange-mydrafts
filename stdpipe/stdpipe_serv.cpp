#include <iostream>
#include <string>
#include <stdexcept>
#include <fstream>
#include <unistd.h>
#include <memory>

class StdPipeServer {
private:
    std::unique_ptr<std::ifstream> cmd_in_file;   // command input pipe
    std::unique_ptr<std::ofstream> cmd_out_file;  // command output pipe

public:
    StdPipeServer(int cmd_in_fd, int cmd_out_fd) {
        std::cerr << "Program starting - StdPipe Server initializing with FDs: "
                  << cmd_in_fd << ", " << cmd_out_fd << std::endl;

        if (cmd_in_fd == 0 && cmd_out_fd == 1) {
            // Special case: using stdin/stdout directly
            cmd_in_file = nullptr;
            cmd_out_file = nullptr;
            std::cerr << "Program starting - Using stdin/stdout directly" << std::endl;
        } else {
            // Open file streams for the anonymous pipes using /proc/self/fd/
            std::string cmd_in_path = "/proc/self/fd/" + std::to_string(cmd_in_fd);
            std::string cmd_out_path = "/proc/self/fd/" + std::to_string(cmd_out_fd);

            cmd_in_file = std::make_unique<std::ifstream>(cmd_in_path);
            if (!cmd_in_file->is_open() || cmd_in_file->fail()) {
                std::cerr << "Error: Failed to open command input pipe (FD " << cmd_in_fd << ")" << std::endl;
                throw std::runtime_error("Failed to open command input pipe");
            }

            cmd_out_file = std::make_unique<std::ofstream>(cmd_out_path);
            if (!cmd_out_file->is_open() || cmd_out_file->fail()) {
                std::cerr << "Error: Failed to open command output pipe (FD " << cmd_out_fd << ")" << std::endl;
                throw std::runtime_error("Failed to open command output pipe");
            }
        }

        std::cerr << "Program starting - StdPipe Server initialized successfully" << std::endl;
    }

    void send_reply(const std::string& reply) {
        std::cerr << "Program sending reply: " << reply << std::endl;
        
        if (cmd_out_file) {
            (*cmd_out_file) << reply << std::endl;
            if (cmd_out_file->fail()) {
                std::cerr << "Error: Failed to write reply to command output pipe" << std::endl;
                throw std::runtime_error("Failed to write to command output pipe");
            }
            cmd_out_file->flush();
        } else {
            // Using stdout directly
            std::cout << reply << std::endl;
            std::cout.flush();
        }
    }

    void main_loop() {
        std::string command;
        
        while (true) {
            std::cerr << "Program waiting for command..." << std::endl;
            
            bool read_success;
            if (cmd_in_file) {
                read_success = static_cast<bool>(std::getline(*cmd_in_file, command));
            } else {
                // Using stdin directly
                read_success = static_cast<bool>(std::getline(std::cin, command));
            }
            
            if (!read_success) {
                if ((cmd_in_file && cmd_in_file->eof()) || (!cmd_in_file && std::cin.eof())) {
                    std::cerr << "Program detected end of input - exiting loop" << std::endl;
                    break;
                } else {
                    std::cerr << "Error: Failed to read from command input pipe" << std::endl;
                    throw std::runtime_error("Failed to read from command input pipe");
                }
            }
            
            std::cerr << "Program getting a command: '" << command << "'" << std::endl;
            
            if (command == "ping") {
                send_reply("pong");
            } else if (command == "quit") {
                std::cerr << "Program received quit command - exiting loop" << std::endl;
                send_reply("goodbye");
                break;
            } else {
                std::cerr << "Unknown command received: '" << command << "'" << std::endl;
                send_reply("command unknown");
            }
        }
        
        std::cerr << "Program exiting main loop" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    try {
        // Expect command line arguments for the two pipe file descriptors
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <cmd_in_fd> <cmd_out_fd>" << std::endl;
            std::cerr << "Error: Expected exactly 2 file descriptors for anonymous pipes" << std::endl;
            throw std::runtime_error("Invalid command line arguments");
        }

        int cmd_in_fd = std::stoi(argv[1]);
        int cmd_out_fd = std::stoi(argv[2]);
        std::cerr << "Will talk CMD on: cmd-in fd " << cmd_in_fd << ", cmd-out fd " << cmd_out_fd << "\n";

        if (cmd_in_fd < 0 || cmd_out_fd < 0) {
            std::cerr << "Error: Invalid file descriptor numbers" << std::endl;
            throw std::runtime_error("Invalid file descriptor numbers");
        }

        StdPipeServer server(cmd_in_fd, cmd_out_fd);
        server.main_loop();
        std::cout << "Program exiting normally" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
        return 2;
    }
}

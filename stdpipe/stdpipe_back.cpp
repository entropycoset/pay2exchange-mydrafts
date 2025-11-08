#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <memory>

class StdPipeBack {
private:
    std::unique_ptr<std::ofstream> cmd_out_file;  // command output pipe
    std::unique_ptr<std::ifstream> resp_in_file;  // response input pipe

public:
    StdPipeBack(int cmd_out_fd, int resp_in_fd) {
        std::cerr << "stdpipe_back: Starting with FDs: "
                  << cmd_out_fd << ", " << resp_in_fd << std::endl;

        // Open file streams for the anonymous pipes
        std::string cmd_out_path = "/proc/self/fd/" + std::to_string(cmd_out_fd);
        std::string resp_in_path = "/proc/self/fd/" + std::to_string(resp_in_fd);

        cmd_out_file = std::make_unique<std::ofstream>(cmd_out_path);
        if (!cmd_out_file->is_open() || cmd_out_file->fail()) {
            std::cerr << "Error: Failed to open command output pipe (FD " << cmd_out_fd << ")" << std::endl;
            throw std::runtime_error("Failed to open command output pipe");
        }

        resp_in_file = std::make_unique<std::ifstream>(resp_in_path);
        if (!resp_in_file->is_open() || resp_in_file->fail()) {
            std::cerr << "Error: Failed to open response input pipe (FD " << resp_in_fd << ")" << std::endl;
            throw std::runtime_error("Failed to open response input pipe");
        }

        std::cerr << "stdpipe_back: Initialized successfully" << std::endl;
    }

    void send_command(const std::string& command) {
        std::cerr << "stdpipe_back: Sending command: " << command << std::endl;
        (*cmd_out_file) << command << std::endl;
        if (cmd_out_file->fail()) {
            std::cerr << "Error: Failed to write command to output pipe" << std::endl;
            throw std::runtime_error("Failed to write command to output pipe");
        }
        cmd_out_file->flush();
    }

    std::string read_response() {
        std::string response;
        if (!std::getline(*resp_in_file, response)) {
            if (resp_in_file->eof()) {
                std::cerr << "stdpipe_back: Response pipe closed" << std::endl;
                return "";
            } else {
                std::cerr << "Error: Failed to read response from input pipe" << std::endl;
                throw std::runtime_error("Failed to read response from input pipe");
            }
        }
        std::cerr << "stdpipe_back: Received response: " << response << std::endl;
        return response;
    }

    void run_test() {
        std::cerr << "stdpipe_back: Starting command generator test" << std::endl;
        
        // Test sequence with responses
        send_command("ping");
        read_response();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        send_command("ping");
        read_response();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        send_command("hello");
        read_response();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        send_command("quit");
        read_response();
        
        std::cerr << "stdpipe_back: Test completed, exiting" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    try {
        // Expect command line arguments for the two pipe file descriptors
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <cmd_out_fd> <resp_in_fd>" << std::endl;
            std::cerr << "Error: Expected exactly 2 file descriptors for pipes" << std::endl;
            throw std::runtime_error("Invalid command line arguments");
        }

        int cmd_out_fd = std::stoi(argv[1]);
        int resp_in_fd = std::stoi(argv[2]);

        if (cmd_out_fd < 0 || resp_in_fd < 0) {
            std::cerr << "Error: Invalid file descriptor numbers" << std::endl;
            throw std::runtime_error("Invalid file descriptor numbers");
        }

        StdPipeBack back(cmd_out_fd, resp_in_fd);
        back.run_test();
        
        std::cerr << "stdpipe_back: Exiting normally" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
        return 2;
    }
}

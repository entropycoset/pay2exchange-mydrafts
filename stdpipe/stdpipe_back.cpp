#include <iostream>
#include <string>
#include <stdexcept>
#include <boost/process.hpp>
#include <thread>
#include <chrono>

namespace bp = boost::process;

class StdPipeController {
private:
    bp::opstream cmd_out;     // Write commands to server
    bp::ipstream resp_in;     // Read responses from server
    bp::child server_process; // The stdpipe_serv process

public:
    StdPipeController(const std::string& server_path) {
        std::cerr << "StdPipeController: Starting server process: " << server_path << std::endl;
        
        try {
            // Launch the server process with stdin/stdout redirected
            // We'll use stdin=FD3, stdout=FD4 approach
            server_process = bp::child(
                server_path + " 0 1",  // Server will use stdin as FD3, stdout as FD4
                bp::std_in < cmd_out,
                bp::std_out > resp_in,
                bp::std_err > stderr
            );
            
            if (!server_process.running()) {
                throw std::runtime_error("Failed to start server process");
            }
            
            std::cerr << "StdPipeController: Server process started successfully" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error starting server process: " << e.what() << std::endl;
            throw;
        }
    }

    ~StdPipeController() {
        if (server_process.running()) {
            std::cerr << "StdPipeController: Terminating server process" << std::endl;
            server_process.terminate();
            server_process.wait();
        }
    }

    void send_command(const std::string& command) {
        std::cerr << "StdPipeController: Sending command: " << command << std::endl;
        cmd_out << command << std::endl;
        cmd_out.flush();
        
        if (cmd_out.fail()) {
            throw std::runtime_error("Failed to send command to server");
        }
    }

    std::string read_response() {
        std::string response;
        if (!std::getline(resp_in, response)) {
            if (resp_in.eof()) {
                throw std::runtime_error("Server closed response pipe");
            } else {
                throw std::runtime_error("Failed to read response from server");
            }
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
            cmd_out.pipe().close();
            resp_in.pipe().close();
            
            // Wait for server to exit
            server_process.wait();
            
            if (server_process.exit_code() != 0) {
                throw std::runtime_error("Server process exited with code: " + 
                                       std::to_string(server_process.exit_code()));
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

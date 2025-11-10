#include <iostream>
#include <string>
#include <stdexcept>
#include <fstream>
#include <unistd.h>
#include <memory>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>



#include <sstream>
#include <type_traits>
#include <limits>
#include <cstdlib>
#include <cerrno>

namespace utils {

template<typename TInt>
TInt parse_strict_integer(const std::string& input) {
    static_assert(std::is_integral<TInt>::value, "TInt must be an integral type");

    if (input.empty()) {
        throw std::invalid_argument("Empty string");
    }

    char* end = nullptr;
    errno = 0;

    // Generic normalization lambda
    auto normalize_and_compare = [&](TInt value) -> TInt {
        std::ostringstream oss;
        oss << value;
        std::string normalized = oss.str();

        if (input != normalized && input != ("+" + normalized)) {
            throw std::invalid_argument("Not normal integer string (junk besides the integer)");
        }
        return value;
    };

    if constexpr (std::is_unsigned<TInt>::value) {
        // --- Unsigned branch ---
        static_assert(std::numeric_limits<TInt>::max() <= std::numeric_limits<unsigned long long>::max(),
                      "TInt too large for strtoull");

        unsigned long long raw = std::strtoull(input.c_str(), &end, 10);

        if (*end != '\0') {
            throw std::invalid_argument("Not normal integer string (junk besides the integer)");
        }
        if (errno == ERANGE || raw > std::numeric_limits<TInt>::max()) {
            throw std::out_of_range("Value out of range for unsigned type");
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
            throw std::invalid_argument("Not normal integer string (junk besides the integer)");
        }
        if (errno == ERANGE || raw < std::numeric_limits<TInt>::min() || raw > std::numeric_limits<TInt>::max()) {
            throw std::out_of_range("Value out of range for signed type");
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
    
    std::unique_ptr<fd_stream_in> cmd_in_file;   // command input pipe
    std::unique_ptr<fd_stream_out> cmd_out_file; // command output pipe

public:
    StdPipeServer(int cmd_in_fd, int cmd_out_fd) {
        std::cerr << "Program starting - StdPipe Server initializing with FDs: "
                  << cmd_in_fd << ", " << cmd_out_fd << "\n";

        // Create boost::iostreams from native file descriptors
        try {
            cmd_in_file = std::make_unique<fd_stream_in>(cmd_in_fd, boost::iostreams::never_close_handle);
            if (!cmd_in_file->is_open()) {
                std::cerr << "Error: Failed to open command input pipe (FD " << cmd_in_fd << ")\n";
                throw std::runtime_error("Failed to open command input pipe");
            }

            cmd_out_file = std::make_unique<fd_stream_out>(cmd_out_fd, boost::iostreams::never_close_handle);
            if (!cmd_out_file->is_open()) {
                std::cerr << "Error: Failed to open command output pipe (FD " << cmd_out_fd << ")\n";
                throw std::runtime_error("Failed to open command output pipe");
            }
        } catch (const std::exception& e) {
            std::cerr << "Error creating boost::iostreams from FDs: " << e.what() << "\n";
            throw;
        }

        std::cerr << "Program starting - StdPipe Server initialized successfully\n";
    }

    void send_reply(const std::string& reply) {
        std::cerr << "Program sending reply: " << reply << "\n";
        
        (*cmd_out_file) << reply << "\n";
        if (cmd_out_file->fail()) {
            std::cerr << "Error: Failed to write reply to command output pipe\n";
            throw std::runtime_error("Failed to write to command output pipe");
        }
        cmd_out_file->flush();
    }

    void main_loop() {
        std::string command;
        
        while (true) {
            std::cerr << "Program waiting for command...\n";
            
            bool read_success = static_cast<bool>(std::getline(*cmd_in_file, command));
            
            if (!read_success) {
                if (cmd_in_file->eof()) {
                    std::cerr << "Program detected end of input - exiting loop\n";
                    break;
                } else {
                    std::cerr << "Error: Failed to read from command input pipe\n";
                    throw std::runtime_error("Failed to read from command input pipe");
                }
            }
            
            std::cerr << "Program getting a command: '" << command << "'\n";
            
            if (command == "ping") {
                send_reply("pong");
            } else if (command == "quit") {
                std::cerr << "Program received quit command - exiting loop\n";
                send_reply("goodbye");
                break;
            } else {
                std::cerr << "Unknown command received: '" << command << "'\n";
                send_reply("command unknown");
            }
        }
        
        std::cerr << "Program exiting main loop\n";
    }
};

int main(int argc, char* argv[]) {
    try {
        // Expect command line arguments for the two pipe file descriptors
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <cmd_in_fd> <cmd_out_fd>\n";
            std::cerr << "Error: Expected exactly 2 file descriptors for anonymous pipes\n";
            throw std::runtime_error("Invalid command line arguments");
        }

        int cmd_in_fd = utils::parse_strict_integer<int>(argv[1]);
        int cmd_out_fd = utils::parse_strict_integer<int>(argv[2]);
        std::cerr << "Will talk CMD on: cmd-in fd " << cmd_in_fd << ", cmd-out fd " << cmd_out_fd << "\n";

        if (cmd_in_fd < 0 || cmd_out_fd < 0) {
            std::cerr << "Error: Invalid file descriptor numbers\n";
            throw std::runtime_error("Invalid file descriptor numbers");
        }

        StdPipeServer server(cmd_in_fd, cmd_out_fd);
        server.main_loop();
        std::cout << "Program exiting normally\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught\n";
        return 2;
    }
}

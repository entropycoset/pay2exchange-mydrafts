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
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include "libcmdformat/libcmdformat.hpp"

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
    libcmdformat::CmdFormat cmd_format = libcmdformat::CmdFormat::cmdformat_v1lenend; // Use v1lenend format

public:
    StdPipeServer(int cmd_in_fd, int cmd_out_fd) {
        std::cerr << "\033[0mProgram starting - StdPipe Server initializing with FDs: "
                  << cmd_in_fd << ", " << cmd_out_fd << "\033[0m\n";

        // Create boost::iostreams from native file descriptors
        try {
            cmd_in_file = std::make_unique<fd_stream_in>(cmd_in_fd, boost::iostreams::never_close_handle);
            if (!cmd_in_file->is_open()) {
                std::cerr << "\033[0mError: Failed to open command input pipe (FD " << cmd_in_fd << ")\033[0m\n";
                throw std::runtime_error("Failed to open command input pipe");
            }

            cmd_out_file = std::make_unique<fd_stream_out>(cmd_out_fd, boost::iostreams::never_close_handle);
            if (!cmd_out_file->is_open()) {
                std::cerr << "\033[0mError: Failed to open command output pipe (FD " << cmd_out_fd << ")\033[0m\n";
                throw std::runtime_error("Failed to open command output pipe");
            }
        } catch (const std::exception& e) {
            std::cerr << "\033[0mError creating boost::iostreams from FDs: " << e.what() << "\033[0m\n";
            throw;
        }

        std::cerr << "\033[0mProgram starting - StdPipe Server initialized successfully\033[0m\n";
    }

    void send_reply(const std::string& reply) {
        std::cerr << "\033[0mProgram sending reply: " << reply << "\033[0m\n";
        
        // Use libcmdformat to encode the reply
        std::string formatted_reply = libcmdformat::encode_command(reply, cmd_format);
        
        cmd_out_file->write(formatted_reply.c_str(), formatted_reply.length());
        if (cmd_out_file->fail()) {
            std::cerr << "\033[0mError: Failed to write reply to command output pipe\033[0m\n";
            throw std::runtime_error("Failed to write to command output pipe");
        }
        cmd_out_file->flush();
    }

    void main_loop() {
        while (true) {
            std::cerr << "\033[0mProgram waiting for command...\033[0m\n";
            
            try {
                // Use libcmdformat to decode the command
                std::string command = libcmdformat::decode_command(*cmd_in_file, cmd_format);
                
                std::cerr << "\033[0mProgram getting a command: '" << command << "'\033[0m\n";
                
                if (command == "ping") {
                    send_reply("pong");
                } else if (command == "quit") {
                    std::cerr << "\033[0mProgram received quit command - exiting loop\033[0m\n";
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
                        
                        std::cerr << "\033[0mProgram sleeping for " << milliseconds << " milliseconds\033[0m\n";
                        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
                        std::cerr << "\033[0mProgram finished sleeping\033[0m\n";
                        
                        send_reply("slept " + std::to_string(milliseconds) + " ms");
                    } catch (const std::exception& e) {
                        std::cerr << "\033[0mError parsing sleep parameter: " << e.what() << "\033[0m\n";
                        send_reply("invalid sleep parameter: " + std::string(e.what()));
                    }
                } else {
                    std::cerr << "\033[0mUnknown command received: '" << command << "'\033[0m\n";
                    send_reply("command unknown");
                }
            } catch (const std::exception& e) {
                std::cerr << "\033[0mError reading/decoding command: " << e.what() << "\033[0m\n";
                // Check if it's EOF or a real error
                if (cmd_in_file->eof()) {
                    std::cerr << "\033[0mProgram detected end of input - exiting loop\033[0m\n";
                    break;
                } else {
                    // For other errors, we might want to continue or exit depending on the error
                    std::cerr << "\033[0mContinuing after command decode error...\033[0m\n";
                    continue;
                }
            }
        }
        
        std::cerr << "\033[0mProgram exiting main loop\033[0m\n";
    }
};

void print_usage(const std::string& program_name) {
    std::cout << "StdPipe Server\n\n";
    std::cout << "Usage: " << program_name << " <cmd_in_fd> <cmd_out_fd>\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  cmd_in_fd    File descriptor number for command input pipe (required)\n";
    std::cout << "  cmd_out_fd   File descriptor number for command output pipe (required)\n\n";
    std::cout << "Description:\n";
    std::cout << "  Server process that communicates via anonymous pipes using the provided\n";
    std::cout << "  file descriptors. Typically spawned by stdpipe_back controller.\n\n";
    std::cout << "Commands:\n";
    std::cout << "  ping         Responds with 'pong'\n";
    std::cout << "  sleep N      Sleeps for N milliseconds (max 10000) then responds\n";
    std::cout << "  quit         Responds with 'goodbye' and exits\n";
    std::cout << "  <unknown>    Responds with 'command unknown'\n\n";
    std::cout << "Note:\n";
    std::cout << "  This program is usually not run directly but launched by stdpipe_back\n";
    std::cout << "  which creates the pipes and passes the file descriptor numbers.\n\n";
}

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
        
        // Expect command line arguments for the two pipe file descriptors
        if (argvect.size() != 3) {
            print_usage(argvect.at(0));
            std::cerr << "Error: Expected exactly 2 file descriptors for anonymous pipes\n";
            throw std::runtime_error("Invalid command line arguments");
        }

        int cmd_in_fd = utils::parse_strict_integer<int>(argvect.at(1));
        int cmd_out_fd = utils::parse_strict_integer<int>(argvect.at(2));
        std::cerr << "\033[0mWill talk CMD on: cmd-in fd " << cmd_in_fd << ", cmd-out fd " << cmd_out_fd << "\033[0m\n";

        if (cmd_in_fd < 0 || cmd_out_fd < 0) {
            std::cerr << "\033[0mError: Invalid file descriptor numbers\033[0m\n";
            throw std::runtime_error("Invalid file descriptor numbers");
        }

        StdPipeServer server(cmd_in_fd, cmd_out_fd);
        server.main_loop();
        std::cout << "\033[0mProgram exiting normally\033[0m\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\033[0mException caught: " << e.what() << "\033[0m\n";
        return 1;
    } catch (...) {
        std::cerr << "\033[0mUnknown exception caught\033[0m\n";
        return 2;
    }
}

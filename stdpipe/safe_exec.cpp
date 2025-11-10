#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "envcleaner.hpp"

class SafeExecutor {
private:
    pid_t child_pid = -1;
    
public:
    ~SafeExecutor() {
        if (child_pid > 0) {
            std::cerr << "SafeExecutor: Cleaning up child process " << child_pid << "\n";
            kill(child_pid, SIGTERM);
            waitpid(child_pid, nullptr, 0);
        }
    }
    
    int execute(const std::string& program, const std::vector<std::string>& args) {
        std::cerr << "SafeExecutor: Executing " << program;
        for (const auto& arg : args) {
            std::cerr << " " << arg;
        }
        std::cerr << "\n";
        
        child_pid = fork();
        if (child_pid == -1) {
            throw std::runtime_error("Failed to fork process");
        }
        
        if (child_pid == 0) {
            // Child process
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(program.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            
            execv(program.c_str(), argv.data());
            _exit(1); // execv failed
        }
        
        // Parent process - wait for child
        int status;
        waitpid(child_pid, &status, 0);
        child_pid = -1; // Child has exited
        
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            return -1; // Child terminated abnormally
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <program> [args...]\n";
            std::cerr << "       " << argv[0] << " --test-fd-management\n";
            return 1;
        }
        
        // Special test mode for FD management functions
        if (std::string(argv[1]) == "--test-fd-management") {
            std::cerr << "=== Testing FD Management Functions ===\n";
            
            // Count initial FDs
            size_t initial_fds = envcleaner::count_open_fd();
            std::cerr << "Initial open FDs: " << initial_fds << "\n";
            
            // Open some extra FDs for testing
            std::cerr << "Opening some test FDs...\n";
            int test_fd1 = open("/dev/null", O_RDONLY);
            int test_fd2 = open("/dev/null", O_WRONLY);
            int test_fd3 = open("/dev/zero", O_RDONLY);
            
            if (test_fd1 == -1 || test_fd2 == -1 || test_fd3 == -1) {
                throw std::runtime_error("Failed to open test FDs");
            }
            
            std::cerr << "Opened test FDs: " << test_fd1 << ", " << test_fd2 << ", " << test_fd3 << "\n";
            
            size_t after_open_fds = envcleaner::count_open_fd();
            std::cerr << "FDs after opening test files: " << after_open_fds << "\n";
            
            // Keep only stdin(0), stdout(1), stderr(2), and test_fd1
            std::vector<int> allowed_fds = {0, 1, 2, test_fd1};
            std::cerr << "Closing all FDs except: ";
            for (int fd : allowed_fds) {
                std::cerr << fd << " ";
            }
            std::cerr << "\n";
            
            size_t closed_count = envcleaner::close_unwanted_fds(allowed_fds);
            
            size_t final_fds = envcleaner::count_open_fd();
            std::cerr << "Final open FDs: " << final_fds << "\n";
            std::cerr << "Total closed FDs: " << closed_count << "\n";
            
            // Clean up the remaining test FD
            close(test_fd1);
            
            std::cerr << "=== FD Management Test Completed Successfully ===\n";
            return 0;
        }
        
        std::string program = argv[1];
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        
        SafeExecutor executor;
        int exit_code = executor.execute(program, args);
        
        std::cerr << "SafeExecutor: Program exited with code " << exit_code << "\n";
        return exit_code;
        
    } catch (const std::exception& e) {
        std::cerr << "SafeExecutor error: " << e.what() << "\n";
        return 1;
    }
}
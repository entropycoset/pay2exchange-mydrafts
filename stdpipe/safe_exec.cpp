#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

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
            return 1;
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
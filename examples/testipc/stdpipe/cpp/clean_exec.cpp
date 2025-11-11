#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <boost/program_options.hpp>
#include "envcleaner.hpp"

namespace po = boost::program_options;

class CleanExecutor {
private:
    pid_t child_pid = -1;
    
public:
    ~CleanExecutor() {
        if (child_pid > 0) {
            std::cerr << "CleanExecutor: Cleaning up child process " << child_pid << "\n";
            kill(child_pid, SIGTERM);
            waitpid(child_pid, nullptr, 0);
        }
    }
    
    int execute(const std::string& program, const std::vector<std::string>& args) {
        std::cerr << "CleanExecutor: Executing " << program;
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

int main_tests() {
    try {
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
        
    } catch (const std::exception& e) {
        std::cerr << "Test error: " << e.what() << "\n";
        return 1;
    }
}

struct CleanupOptions {
    std::vector<int> allowed_fds;
    std::vector<std::string> keep_env_vars;
    std::vector<std::pair<std::string, std::string>> set_env_vars;
    bool clean_fds = false;
    bool clean_env = false;
};

void cleanup_child_environment(const CleanupOptions& opts) {
    try {
        // Clean FDs if requested
        if (opts.clean_fds) {
            std::cerr << "Child: Cleaning FDs, keeping: ";
            for (int fd : opts.allowed_fds) {
                std::cerr << fd << " ";
            }
            std::cerr << "\n";
            
            size_t closed = envcleaner::close_unwanted_fds(opts.allowed_fds);
            std::cerr << "Child: Closed " << closed << " FDs\n";
        }
        
        // Clean environment if requested
        if (opts.clean_env) {
            std::cerr << "Child: Cleaning environment, keeping: ";
            for (const std::string& var : opts.keep_env_vars) {
                std::cerr << var << " ";
            }
            std::cerr << "\n";
            
            envcleaner::clean_environment(opts.keep_env_vars);
        }
        
        // Set additional environment variables
        if (!opts.set_env_vars.empty()) {
            std::cerr << "Child: Setting environment variables: ";
            for (const auto& pair : opts.set_env_vars) {
                std::cerr << pair.first << "=" << pair.second << " ";
            }
            std::cerr << "\n";
            
            envcleaner::set_environment(opts.set_env_vars);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Child cleanup error: " << e.what() << "\n";
        _exit(1);
    }
}

int main_run(int argc, char* argv[]) {
    try {
        // First convert argv to safe vector
        std::vector<std::string> argvect;
        for (int i = 0; i < argc; ++i) {
            if (argv[i] == nullptr) {
                throw std::runtime_error("Null argv element at index " + std::to_string(i));
            }
            argvect.push_back(std::string(argv[i]));
        }
        
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help,h", "produce help message")
            ("clean-fd-except", po::value<std::string>(), "comma-separated list of FDs to keep (e.g., '0,1,2')")
            ("clean-env-except", po::value<std::string>(), "comma-separated list of env vars to keep (e.g., 'PATH,USER')")
            ("set-env", po::value<std::string>(), "comma-separated list of env vars to set (e.g., 'HOME=/tmp,VAR=value')")
            ("program", po::value<std::string>()->required(), "program to execute")
            ("program-args", po::value<std::vector<std::string>>(), "arguments for the program");
        
        po::positional_options_description p;
        p.add("program", 1);
        p.add("program-args", -1);
        
        // Skip the first argument ("--run") and parse the rest - use safe access
        if (argvect.size() < 3) {
            throw std::runtime_error("Insufficient arguments for --run mode");
        }
        
        std::vector<std::string> args_vec;
        for (size_t i = 2; i < argvect.size(); ++i) {
            args_vec.push_back(argvect.at(i));
        }
        
        po::variables_map vm;
        try {
            po::store(po::command_line_parser(args_vec)
                     .options(desc)
                     .positional(p)
                     .allow_unregistered() // Allow program args that aren't in our options
                     .run(), vm);
        } catch (const po::error& e) {
            std::cerr << "Argument parsing error: " << e.what() << "\n";
            std::cerr << desc << "\n";
            return 1;
        }
        
        if (vm.count("help")) {
            std::cerr << desc << "\n";
            return 0;
        }
        
        po::notify(vm);
        
        CleanupOptions opts;
        
        // Parse cleanup options
        if (vm.count("clean-fd-except")) {
            opts.allowed_fds = envcleaner::parse_fd_list(vm["clean-fd-except"].as<std::string>());
            opts.clean_fds = true;
        }
        
        if (vm.count("clean-env-except")) {
            opts.keep_env_vars = envcleaner::parse_string_list(vm["clean-env-except"].as<std::string>());
            opts.clean_env = true;
        }
        
        if (vm.count("set-env")) {
            opts.set_env_vars = envcleaner::parse_env_pairs(vm["set-env"].as<std::string>());
        }
        
        std::string program = vm["program"].as<std::string>();
        std::vector<std::string> program_args;
        if (vm.count("program-args")) {
            program_args = vm["program-args"].as<std::vector<std::string>>();
        }
        
        std::cerr << "CleanExecutor: Executing " << program;
        for (const auto& arg : program_args) {
            std::cerr << " " << arg;
        }
        std::cerr << "\n";
        
        // Fork and execute with cleanup
        pid_t child_pid = fork();
        if (child_pid == -1) {
            throw std::runtime_error("Failed to fork process");
        }
        
        if (child_pid == 0) {
            // Child process - perform cleanup then exec
            cleanup_child_environment(opts);
            
            // Prepare arguments for exec
            std::vector<char*> argv_exec;
            argv_exec.push_back(const_cast<char*>(program.c_str()));
            for (const auto& arg : program_args) {
                argv_exec.push_back(const_cast<char*>(arg.c_str()));
            }
            argv_exec.push_back(nullptr);
            
            execv(program.c_str(), argv_exec.data());
            std::cerr << "Child: execv failed: " << strerror(errno) << "\n";
            _exit(1);
        }
        
        // Parent process - wait for child
        int status;
        waitpid(child_pid, &status, 0);
        
        int exit_code;
        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        } else {
            exit_code = -1; // Child terminated abnormally
        }
        
        std::cerr << "CleanExecutor: Program exited with code " << exit_code << "\n";
        return exit_code;
        
    } catch (const std::exception& e) {
        std::cerr << "CleanExecutor error: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    // First convert argv to safe vector
    std::vector<std::string> argvect;
    for (int i = 0; i < argc; ++i) {
        if (argv[i] == nullptr) {
            throw std::runtime_error("Null argv element at index " + std::to_string(i));
        }
        argvect.push_back(std::string(argv[i]));
    }
    
    if (argvect.size() < 2) {
        std::cerr << "Usage: " << argvect.at(0) << " --tests\n";
        std::cerr << "       " << argvect.at(0) << " --run <program> [args...]\n";
        return 1;
    }
    
    std::string mode = argvect.at(1);
    
    if (mode == "--tests") {
        return main_tests();
    } else if (mode == "--run") {
        return main_run(argc, argv);
    } else {
        std::cerr << "Error: First argument must be --tests or --run\n";
        std::cerr << "Usage: " << argvect.at(0) << " --tests\n";
        std::cerr << "       " << argvect.at(0) << " --run <program> [args...]\n";
        return 1;
    }
}
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
#include "libecul/ecul.hpp"

// Override project name for ECUL logging
namespace ecul {
	std::string get_project_name() {
		return "CleanExecApp";
	}
}

class CleanExecutor {
private:
	pid_t child_pid = -1;

public:
	~CleanExecutor() {
		if (child_pid > 0) {
			ecul_info("Cleaning up child process " + std::to_string(child_pid));
			kill(child_pid, SIGTERM);
			waitpid(child_pid, nullptr, 0);
		}
	}

	int execute(const std::string& program, const std::vector<std::string>& args) {
		std::string cmd_line = "Executing " + program;
		for (const auto& arg : args) {
			cmd_line += " " + arg;
		}
		ecul_info(cmd_line);

		child_pid = fork();
		if (child_pid == -1) {
			throw ecul_stop("Failed to fork process - cannot spawn child");
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
			// If we reach here, execv failed - this is a stop error
			ecul_log_stop("execv failed for " + program + ": " + strerror(errno));
			_exit(1);
		}

		// Parent process - wait for child
		int status;
		if (waitpid(child_pid, &status, 0) == -1) {
			throw ecul_erro_runtime("waitpid failed: " + std::string(strerror(errno)));
		}
		child_pid = -1; // Child has exited

		if (WIFEXITED(status)) {
			int exit_code = WEXITSTATUS(status);
			ecul_info("Child exited with code " + std::to_string(exit_code));
			return exit_code;
		} else {
			ecul_warn("Child terminated abnormally");
			return -1;
		}
	}
};

int main_tests() {
	try {
		ecul_info("=== Testing FD Management Functions ===");

		// Count initial FDs
		size_t initial_fds = envcleaner::count_open_fd();
		ecul_info("Initial open FDs: " + std::to_string(initial_fds));

		// Open some extra FDs for testing
		ecul_info("Opening some test FDs...");
		int test_fd1 = open("/dev/null", O_RDONLY);
		int test_fd2 = open("/dev/null", O_WRONLY);
		int test_fd3 = open("/dev/zero", O_RDONLY);

		if (test_fd1 == -1 || test_fd2 == -1 || test_fd3 == -1) {
			throw ecul_erro_runtime("Failed to open test FDs: " + std::string(strerror(errno)));
		}

		ecul_info("Opened test FDs: " + std::to_string(test_fd1) + ", " +
				  std::to_string(test_fd2) + ", " + std::to_string(test_fd3));

		size_t after_open_fds = envcleaner::count_open_fd();
		ecul_info("FDs after opening test files: " + std::to_string(after_open_fds));

		// Keep only stdin(0), stdout(1), stderr(2), and test_fd1
		std::vector<int> allowed_fds = {0, 1, 2, test_fd1};
		std::string allowed_list = "Closing all FDs except: ";
		for (int fd : allowed_fds) {
			allowed_list += std::to_string(fd) + " ";
		}
		ecul_info(allowed_list);

		size_t closed_count;
		try {
			closed_count = envcleaner::close_unwanted_fds(allowed_fds);
		} catch (const std::exception& e) {
			throw ecul_stop("Failed to close unwanted FDs - state not cleaned properly: " + std::string(e.what()));
		}

		size_t final_fds = envcleaner::count_open_fd();
		ecul_info("Final open FDs: " + std::to_string(final_fds));
		ecul_info("Total closed FDs: " + std::to_string(closed_count));

		// Clean up the remaining test FD
		if (close(test_fd1) != 0) {
			ecul_warn("Failed to close test_fd1: " + std::string(strerror(errno)));
		}

		ecul_info("=== FD Management Test Completed Successfully ===");
		return 0;

	} catch (const std::exception& e) {
		ecul_log_erro("Test error: " + std::string(e.what()));
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
			std::string fd_list = "Child: Cleaning FDs, keeping: ";
			for (int fd : opts.allowed_fds) {
				fd_list += std::to_string(fd) + " ";
			}
			ecul_info(fd_list);

			size_t closed;
			try {
				closed = envcleaner::close_unwanted_fds(opts.allowed_fds);
			} catch (const std::exception& e) {
				ecul_log_stop("Child: Failed to close unwanted FDs - cannot clean state: " + std::string(e.what()));
				_exit(1);
			}
			ecul_info("Child: Closed " + std::to_string(closed) + " FDs");
		}

		// Clean environment if requested
		if (opts.clean_env) {
			std::string var_list = "Child: Cleaning environment, keeping: ";
			for (const std::string& var : opts.keep_env_vars) {
				var_list += var + " ";
			}
			ecul_info(var_list);

			try {
				envcleaner::clean_environment(opts.keep_env_vars);
			} catch (const std::exception& e) {
				ecul_log_stop("Child: Failed to clean environment - cannot clean state: " + std::string(e.what()));
				_exit(1);
			}
		}

		// Set additional environment variables
		if (!opts.set_env_vars.empty()) {
			std::string env_list = "Child: Setting environment variables: ";
			for (const auto& pair : opts.set_env_vars) {
				env_list += pair.first + "=" + pair.second + " ";
			}
			ecul_info(env_list);

			try {
				envcleaner::set_environment(opts.set_env_vars);
			} catch (const std::exception& e) {
				ecul_log_stop("Child: Failed to set environment variables - cannot clean state: " + std::string(e.what()));
				_exit(1);
			}
		}

	} catch (const std::exception& e) {
		ecul_log_stop("Child cleanup error: " + std::string(e.what()));
		_exit(1);
	}
}

void print_usage(const std::string& program_name) {
	std::cout << "Clean Exec - Environment and File Descriptor Cleanup Tool\n\n";
	std::cout << "Usage: " << program_name << " <mode> [options]\n\n";
	std::cout << "Modes:\n";
	std::cout << "	--tests												Run FD management tests\n";
	std::cout << "	--run <program> [args...]			Execute program with cleanup options\n";
	std::cout << "	--help												Show this help message\n\n";
	std::cout << "Description:\n";
	std::cout << "	--tests mode:\n";
	std::cout << "		Runs internal tests to verify file descriptor management functions.\n";
	std::cout << "		No additional arguments required.\n\n";
	std::cout << "	--run mode:\n";
	std::cout << "		Executes a program with optional environment and FD cleanup.\n";
	std::cout << "		Requires at least a program path to execute.\n\n";
	std::cout << "		Available cleanup options for --run:\n";
	std::cout << "			--clean-fd-except <fds>			Keep only specified FDs (e.g., '0,1,2')\n";
	std::cout << "			--clean-env-except <vars>		Keep only specified env vars (e.g., 'HOME,USER')\n";
	std::cout << "			--set-env <pairs>						Set env vars (e.g., 'HOME=/tmp,VAR=value')\n\n";
	std::cout << "Examples:\n";
	std::cout << "	" << program_name << " --tests\n";
	std::cout << "	" << program_name << " --run /bin/ls -la\n";
	std::cout << "	" << program_name << " --run --clean-fd-except 0,1,2 /bin/echo hello\n";
	std::cout << "	" << program_name << " --run --clean-env-except HOME,USER /usr/bin/env\n\n";
}

int main_run(int argc, char* argv[]) {
	try {
		// First convert argv to safe vector
		std::vector<std::string> argvect;
		for (int i = 0; i < argc; ++i) {
			if (argv[i] == nullptr) {
				throw ecul_erro_runtime("Null argv element at index " + std::to_string(i));
			}
			argvect.push_back(std::string(argv[i]));
		}

		namespace po = boost::program_options;

		boost::program_options::options_description desc("Allowed options");
		desc.add_options()
			("help,h", "produce help message")
			("clean-fd-except", boost::program_options::value<std::string>(), "comma-separated list of FDs to keep (e.g., '0,1,2')")
			("clean-env-except", boost::program_options::value<std::string>(), "comma-separated list of env vars to keep (e.g., 'PATH,USER')")
			("set-env", boost::program_options::value<std::string>(), "comma-separated list of env vars to set (e.g., 'HOME=/tmp,VAR=value')")
			("program", boost::program_options::value<std::string>()->required(), "program to execute")
			("program-args", boost::program_options::value<std::vector<std::string>>(), "arguments for the program");

		boost::program_options::positional_options_description p;
		p.add("program", 1);
		p.add("program-args", -1);

		// Skip the first argument ("--run") and parse the rest - use safe access
		if (argvect.size() < 3) {
			throw ecul_erro_runtime("--run mode requires at least a program to execute");
		}

		std::vector<std::string> args_vec;
		for (size_t i = 2; i < argvect.size(); ++i) {
			args_vec.push_back(argvect.at(i));
		}

		boost::program_options::variables_map vm;
		try {
			boost::program_options::store(boost::program_options::command_line_parser(args_vec)
						 .options(desc)
						 .positional(p)
						 .allow_unregistered() // Allow program args that aren't in our options
						 .run(), vm);
		} catch (const boost::program_options::error& e) {
			throw ecul_erro_runtime("Argument parsing error: " + std::string(e.what()));
		}

		if (vm.count("help")) {
			std::cout << desc << "\n";
			return 0;
		}

		boost::program_options::notify(vm);

		CleanupOptions opts;

		// Parse cleanup options
		if (vm.count("clean-fd-except")) {
			try {
				opts.allowed_fds = envcleaner::parse_fd_list(vm["clean-fd-except"].as<std::string>());
				opts.clean_fds = true;
			} catch (const std::exception& e) {
				throw ecul_erro_runtime("Failed to parse FD list: " + std::string(e.what()));
			}
		}

		if (vm.count("clean-env-except")) {
			try {
				opts.keep_env_vars = envcleaner::parse_string_list(vm["clean-env-except"].as<std::string>());
				opts.clean_env = true;
			} catch (const std::exception& e) {
				throw ecul_erro_runtime("Failed to parse env var list: " + std::string(e.what()));
			}
		}

		if (vm.count("set-env")) {
			try {
				opts.set_env_vars = envcleaner::parse_env_pairs(vm["set-env"].as<std::string>());
			} catch (const std::exception& e) {
				throw ecul_erro_runtime("Failed to parse env var pairs: " + std::string(e.what()));
			}
		}

		std::string program = vm["program"].as<std::string>();
		std::vector<std::string> program_args;
		if (vm.count("program-args")) {
			program_args = vm["program-args"].as<std::vector<std::string>>();
		}

		std::string cmd_line = "CleanExecutor: Executing " + program;
		for (const auto& arg : program_args) {
			cmd_line += " " + arg;
		}
		ecul_info(cmd_line);

		// Fork and execute with cleanup
		pid_t child_pid = fork();
		if (child_pid == -1) {
			throw ecul_stop("Failed to fork process - cannot spawn child");
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
			ecul_log_stop("Child: execv failed for " + program + ": " + strerror(errno));
			_exit(1);
		}

		// Parent process - wait for child
		int status;
		if (waitpid(child_pid, &status, 0) == -1) {
			throw ecul_erro_runtime("waitpid failed: " + std::string(strerror(errno)));
		}

		int exit_code;
		if (WIFEXITED(status)) {
			exit_code = WEXITSTATUS(status);
			ecul_info("Program exited with code " + std::to_string(exit_code));
		} else if (WIFSIGNALED(status)) {
			int signal = WTERMSIG(status);
			ecul_warn("Program terminated by signal " + std::to_string(signal));
			exit_code = 128 + signal;
		} else {
			ecul_warn("Program terminated abnormally");
			exit_code = -1;
		}

		return exit_code;

	} catch (const std::exception& e) {
		ecul_log_erro("CleanExecutor error: " + std::string(e.what()));
		return 1;
	}
}

int main(int argc, char* argv[]) {
	// Initialize ECUL colors early
	ecul::init_colors();

	// Configure default ECUL logging settings
	auto& log_settings = ecul::get_log_settings();
	log_settings.set_date_format(ecul::DateFormat::no_date);
	log_settings.set_time_format(ecul::TimeFormat::short_time);
	log_settings.set_runtime_format(ecul::RuntimeFormat::ms);
	log_settings.set_program_name_format(ecul::ProgramNameFormat::prefer_bin);
	log_settings.set_line_width(4);
	log_settings.set_spacing_format(ecul::SpacingFormat::normal);
	
	// Configure program icon - light-magenta background, black foreground
	log_settings.set_program_icon(" -clean-");
	log_settings.set_program_icon_usecolor(true);
	log_settings.set_program_icon_fg(static_cast<int>(ecul::Color::Black));
	log_settings.set_program_icon_bg(static_cast<int>(ecul::Color::LightMagenta));
	log_settings.set_program_icon_show(true);

	try {
		// First convert argv to safe vector
		std::vector<std::string> argvect;
		for (int i = 0; i < argc; ++i) {
			if (argv[i] == nullptr) {
				throw ecul_erro_runtime("Null argv element at index " + std::to_string(i));
			}
			const std::string onearg = (argv[i]);
			argvect.push_back(onearg);
			ecul_info( (std::ostringstream{} << "arg[" << i << "] = [" << onearg << "]").str() )  ;
		}

		// Check for --help or insufficient arguments
		if (argvect.size() < 2 || argvect.at(1) == "--help") {
			print_usage(argvect.at(0));
			return argvect.size() < 2 ? 1 : 0;
		}

		std::string mode = argvect.at(1);

		if (mode == "--tests") {
			// No additional arguments needed for tests
			return main_tests();
		} else if (mode == "--run") {
			// Validate that we have at least a program to run
			if (argvect.size() < 3) {
				throw ecul_erro_runtime("--run mode requires at least a program to execute");
			}
			return main_run(argc, argv);
		} else {
			throw ecul_erro_runtime("Unknown mode '" + mode + "'. Must be --tests, --run, or --help");
		}

	} catch (const std::exception& e) {
		ecul_log_erro("Program error: " + std::string(e.what()));
		print_usage(argc > 0 ? argv[0] : "clean_exec");
		return 1;
	}
}
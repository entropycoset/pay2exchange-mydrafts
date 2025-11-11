#include <iostream>
#include <string>
#include <vector>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <type_traits>

extern char **environ;

void print_environment() {
		std::cout << "=== Environment Variables ===\n";
		std::vector<std::string> env_vars;

		if (environ != nullptr) {
				for (char **env = environ; *env != nullptr; env++) {
						env_vars.push_back(std::string(*env));
				}
		}

		// Sort for consistent output
		std::sort(env_vars.begin(), env_vars.end());

		for (const std::string& env_var : env_vars) {
				std::cout << env_var << "\n";
		}

		std::cout << "Total environment variables: " << env_vars.size() << "\n";
		std::cout << "\n";
}

void print_file_descriptors() {
		// Static asserts to ensure FD type safety at compile time
		// File descriptors are int type in POSIX, verify type relationships
		static_assert(sizeof(int) <= sizeof(long),
									"int (FD type) must fit in long (strtol return type)");
		static_assert(std::numeric_limits<int>::max() <= std::numeric_limits<long>::max(),
									"int max value must fit in long for safe strtol parsing");
		static_assert(std::numeric_limits<int>::min() >= std::numeric_limits<long>::min(),
									"int min value must be representable in long");
		static_assert(std::is_signed<int>::value == std::is_signed<long>::value,
									"int and long must have same signedness for safe conversion");

		std::cout << "=== Open File Descriptors ===\n";
		std::vector<int> fds;

		DIR* fd_dir = opendir("/proc/self/fd");
		if (fd_dir == nullptr) {
				std::cerr << "Error: Cannot open /proc/self/fd\n";
				return;
		}

		int scan_fd = dirfd(fd_dir);
		struct dirent* entry;

		while ((entry = readdir(fd_dir)) != nullptr) {
				// Skip "." and ".." entries
				if (entry->d_name[0] == '.') {
						continue;
				}

				// Parse the filename as an FD number
				char* endptr;
				long fd_num = strtol(entry->d_name, &endptr, 10);

				// Skip invalid entries (not pure numbers)
				if (*endptr != '\0' || fd_num < 0) {
						continue;
				}

				// Validate fd_num is within int range before casting
				if (fd_num > INT_MAX) {
						std::cerr << "Warning: FD number " << fd_num << " exceeds INT_MAX, skipping\n";
						continue;
				}

				int fd_as_int = static_cast<int>(fd_num);

				// Skip the FD we're using to scan the directory
				if (fd_as_int == scan_fd) {
						continue;
				}

				fds.push_back(fd_as_int);
		}

		closedir(fd_dir);

		// Sort for consistent output
		std::sort(fds.begin(), fds.end());

		std::cout << "Open FDs: ";
		for (size_t i = 0; i < fds.size(); ++i) {
				if (i > 0) std::cout << ",";
				std::cout << fds.at(i);  // Use .at() for bounds checking
		}
		std::cout << "\n";

		std::cout << "Total open FDs: " << fds.size() << "\n";
		std::cout << "\n";
}

int main() {
		std::cout << "=== print_env_extra Output ===\n";
		print_environment();
		print_file_descriptors();
		std::cout << "=== End Output ===\n";
		return 0;
}

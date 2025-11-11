#include "envcleaner.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <sstream>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <type_traits>
#include <fcntl.h>

extern char **environ;

namespace envcleaner {

struct dirent* safe_readdir(DIR* dir) {
    // For readdir, we need a special validator since nullptr can be valid (end of stream)
    // We check errno after the call - if errno is set, then nullptr indicates error
    errno = 0;
    struct dirent* result = readdir(dir);
    if (result == nullptr && errno != 0) {
        std::cerr << "Error: readdir failed with errno=" << errno << ": " << strerror(errno) << "\n";
        throw std::runtime_error("readdir failed: " + std::string(strerror(errno)));
    }
    return result;
}

DIR* safe_opendir(const char* path) {
    return validated_libc_call<DIR*>(
        opendir,
        [](DIR* result) { return result != nullptr; },
        path
    );
}

void safe_setenv(const char* name, const char* value, int overwrite) {
    validated_libc_call<int>(
        [](const char* n, const char* v, int o) { return setenv(n, v, o); },
        [](int result) { return result == 0; },
        name, value, overwrite
    );
}

void safe_unsetenv(const char* name) {
    validated_libc_call<int>(
        unsetenv,
        [](int result) { return result == 0; },
        name
    );
}

// Helper function to validate that input contains no leading/trailing whitespace
void validate_no_whitespace_padding(const std::string& item, const std::string& context) {
    if (!item.empty()) {
        if (std::isspace(static_cast<unsigned char>(item.front()))) {
            std::cerr << "Error: " << context << " contains leading whitespace: '" << item << "'\n";
            throw std::runtime_error(context + " contains leading whitespace, use normalized input without spaces/tabs");
        }
        if (std::isspace(static_cast<unsigned char>(item.back()))) {
            std::cerr << "Error: " << context << " contains trailing whitespace: '" << item << "'\n";
            throw std::runtime_error(context + " contains trailing whitespace, use normalized input without spaces/tabs");
        }
    }
}

// Helper function to validate POSIX environment variable names
void validate_env_var_name(const std::string& name) {
    if (name.empty()) {
        std::cerr << "Error: Environment variable name cannot be empty\n";
        throw std::runtime_error("Empty environment variable name");
    }

    // POSIX: First character must be letter or underscore
    char first = name[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') {
        std::cerr << "Error: Environment variable name '" << name
                  << "' must start with letter (a-z,A-Z) or underscore (_), got: '" << first << "'\n";
        throw std::runtime_error("Invalid environment variable name: " + name);
    }

    // POSIX: Remaining characters must be letters, digits, or underscores
    for (size_t i = 1; i < name.length(); ++i) {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::cerr << "Error: Environment variable name '" << name
                      << "' contains invalid character at position " << i << ": '" << c
                      << "' (only letters, digits, underscore allowed)\n";
            throw std::runtime_error("Invalid character in environment variable name: " + name);
        }
    }
}

std::vector<int> parse_fd_list(const std::string& input) {
    std::vector<int> result;
    if (input.empty()) {
        return result;
    }

    std::istringstream iss(input);
    std::string item;

    while (std::getline(iss, item, ',')) {
        // Strict validation: NO silent trimming, throw on non-normalized input
        validate_no_whitespace_padding(item, "FD number");

        if (item.empty()) {
            std::cerr << "Error: Empty FD number in comma-separated list\n";
            throw std::runtime_error("Empty FD number in list");
        }

        // Parse as integer with strict validation
        char* endptr;
        errno = 0;
        long fd_num = std::strtol(item.c_str(), &endptr, 10);

        if (*endptr != '\0') {
            std::cerr << "Error: Invalid FD number contains non-digit characters: '" << item << "'\n";
            throw std::runtime_error("Invalid FD number: " + item);
        }
        if (errno == ERANGE) {
            std::cerr << "Error: FD number caused numerical overflow: '" << item << "'\n";
            throw std::runtime_error("FD number caused overflow: " + item);
        }
        if (fd_num < 0) {
            std::cerr << "Error: Negative FD number not allowed: " << fd_num << "\n";
            throw std::runtime_error("Negative FD number: " + item);
        }
        if (fd_num > INT_MAX) {
            std::cerr << "Error: FD number exceeds maximum allowed value (INT_MAX=" << INT_MAX << "): " << fd_num << "\n";
            throw std::runtime_error("FD number exceeds INT_MAX: " + item);
        }

        result.push_back(static_cast<int>(fd_num));
    }

    return result;
}

std::vector<std::string> parse_string_list(const std::string& input) {
    std::vector<std::string> result;
    if (input.empty()) {
        return result;
    }

    std::istringstream iss(input);
    std::string item;

    while (std::getline(iss, item, ',')) {
        // Strict validation: NO silent trimming, throw on non-normalized input
        validate_no_whitespace_padding(item, "Environment variable name");

        if (item.empty()) {
            std::cerr << "Error: Empty environment variable name in comma-separated list\n";
            throw std::runtime_error("Empty string in list");
        }

        // Comprehensive POSIX environment variable name validation
        validate_env_var_name(item);

        result.push_back(item);
    }

    return result;
}

std::vector<std::pair<std::string, std::string>> parse_env_pairs(const std::string& input) {
    std::vector<std::pair<std::string, std::string>> result;
    if (input.empty()) {
        return result;
    }

    std::istringstream iss(input);
    std::string item;

    while (std::getline(iss, item, ',')) {
        // Strict validation: NO silent trimming, throw on non-normalized input
        validate_no_whitespace_padding(item, "Key=value pair");

        if (item.empty()) {
            std::cerr << "Error: Empty key=value pair in comma-separated list\n";
            throw std::runtime_error("Empty key=value pair in list");
        }

        size_t eq_pos = item.find('=');
        if (eq_pos == std::string::npos) {
            std::cerr << "Error: Missing '=' separator in key=value pair: '" << item << "'\n";
            throw std::runtime_error("Missing '=' in key=value pair: " + item);
        }
        if (eq_pos == 0) {
            std::cerr << "Error: Empty key in key=value pair: '" << item << "'\n";
            throw std::runtime_error("Empty key in key=value pair: " + item);
        }

        std::string key = item.substr(0, eq_pos);
        std::string value = item.substr(eq_pos + 1);

        // Strict validation: NO silent trimming of key/value
        validate_no_whitespace_padding(key, "Environment variable key");
        // Note: Values can contain spaces, so we don't validate value padding

        if (key.empty()) {
            std::cerr << "Error: Environment variable key is empty in: '" << item << "'\n";
            throw std::runtime_error("Empty key in: " + item);
        }

        // Comprehensive POSIX environment variable name validation for key
        validate_env_var_name(key);

        result.push_back(std::make_pair(key, value));
    }

    return result;
}

void clean_environment(const std::vector<std::string>& keep_vars) {
    // Get all current environment variables
    std::vector<std::string> to_unset;

    if (environ == nullptr) {
        return; // No environment to clean
    }

    // Collect names of all current environment variables
    for (char **env = environ; *env != nullptr; env++) {
        std::string env_str(*env);
        size_t eq_pos = env_str.find('=');
        if (eq_pos != std::string::npos) {
            std::string var_name = env_str.substr(0, eq_pos);

            // Check if this variable should be kept
            bool should_keep = false;
            for (const std::string& keep_var : keep_vars) {
                if (var_name == keep_var) {
                    should_keep = true;
                    break;
                }
            }

            if (!should_keep) {
                to_unset.push_back(var_name);
            }
        }
    }

    // Unset variables that shouldn't be kept
    for (const std::string& var_name : to_unset) {
        safe_unsetenv(var_name.c_str());
    }
}

void set_environment(const std::vector<std::pair<std::string, std::string>>& env_vars) {
    for (const auto& pair : env_vars) {
        safe_setenv(pair.first.c_str(), pair.second.c_str(), 1); // overwrite = 1
    }
}

size_t count_open_fd() {
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

    DIR* fd_dir = safe_opendir("/proc/self/fd");

    int scan_fd = dirfd(fd_dir);
    if (scan_fd == -1) {
        std::cerr << "Error: Failed to get directory FD: " << strerror(errno) << "\n";
        closedir(fd_dir);
        throw std::runtime_error("Failed to get directory FD");
    }

    size_t count = 0;
    struct dirent* entry;
    errno = 0; // Clear errno before readdir loop

    while ((entry = safe_readdir(fd_dir)) != nullptr) {
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

        // Skip entries that exceed int range (FDs should fit in int)
        if (fd_num > INT_MAX) {
            std::cerr << "Warning: FD number " << fd_num << " exceeds INT_MAX in count_open_fd, skipping\n";
            continue;
        }

        int fd_as_int = static_cast<int>(fd_num);

        // Skip the FD we're using to scan the directory
        if (fd_as_int == scan_fd) {
            continue;
        }

        count++;
    }

    // No need to check errno here since safe_readdir handles it

    if (closedir(fd_dir) != 0) {
        std::cerr << "Error: Failed to close /proc/self/fd directory: " << strerror(errno) << "\n";
        throw std::runtime_error("Failed to close /proc/self/fd directory");
    }

    return count;
}

size_t close_unwanted_fds(std::vector<int> fd_allowed) {
    // Static asserts for FD type safety (same as count_open_fd)
    // File descriptors are int type in POSIX, verify type relationships
    static_assert(sizeof(int) <= sizeof(long),
                  "int (FD type) must fit in long (strtol return type)");
    static_assert(std::numeric_limits<int>::max() <= std::numeric_limits<long>::max(),
                  "int max value must fit in long for safe strtol parsing");
    static_assert(std::numeric_limits<int>::min() >= std::numeric_limits<long>::min(),
                  "int min value must be representable in long");
    static_assert(std::is_signed<int>::value == std::is_signed<long>::value,
                  "int and long must have same signedness for safe conversion");

    // Deduplicate and sort the allowed FDs vector
    std::sort(fd_allowed.begin(), fd_allowed.end());
    fd_allowed.erase(std::unique(fd_allowed.begin(), fd_allowed.end()), fd_allowed.end());

    std::cerr << "Allowed FDs: ";
    for (int fd : fd_allowed) {
        std::cerr << fd << " ";
    }
    std::cerr << "\n";

    // Open directory to scan FDs
    DIR* fd_dir = safe_opendir("/proc/self/fd");

    int scan_fd = dirfd(fd_dir);
    if (scan_fd == -1) {
        std::cerr << "Error: Failed to get directory FD for closing: " << strerror(errno) << "\n";
        closedir(fd_dir);
        throw std::runtime_error("Failed to get directory FD for closing");
    }

    // Collect all open FDs first (except our scan FD)
    std::vector<int> open_fds;
    struct dirent* entry;
    errno = 0;

    while ((entry = safe_readdir(fd_dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char* endptr;
        long fd_num = strtol(entry->d_name, &endptr, 10);

        if (*endptr != '\0' || fd_num < 0) {
            continue;
        }

        // Validate fd_num is within int range before casting
        if (fd_num > INT_MAX) {
            std::cerr << "Warning: FD number " << fd_num << " exceeds INT_MAX in close_unwanted_fds, skipping\n";
            continue;
        }

        int fd_as_int = static_cast<int>(fd_num);

        if (fd_as_int == scan_fd) {
            continue;
        }

        open_fds.push_back(fd_as_int);
    }

    // No need to check errno here since safe_readdir handles it

    // Close the scan directory now
    if (closedir(fd_dir) != 0) {
        std::cerr << "Error: Failed to close /proc/self/fd directory after scanning: " << strerror(errno) << "\n";
        throw std::runtime_error("Failed to close /proc/self/fd directory after scanning");
    }

    // Close unwanted FDs
    size_t closed_count = 0;
    for (int fd : open_fds) {
        // Check if this FD is in the allowed list
        if (std::binary_search(fd_allowed.begin(), fd_allowed.end(), fd)) {
            continue; // Keep this FD open
        }

        // Close this FD
        if (close(fd) != 0) {
            std::cerr << "Error: Failed to close FD " << fd << ": " << strerror(errno) << "\n";
            throw std::runtime_error("Failed to close unwanted FD " + std::to_string(fd));
        }
        closed_count++;
        std::cerr << "Closed FD " << fd << "\n";
    }

    // Verify the final state
    size_t remaining_fds = count_open_fd();
    if (remaining_fds > fd_allowed.size()) {
        std::cerr << "Error: Too many FDs remain open. Expected <= " << fd_allowed.size()
                  << ", got " << remaining_fds << "\n";
        throw std::runtime_error("More FDs remain open than allowed");
    }

    // Final verification: enumerate all opened FDs and confirm each is in allowed list
    DIR* verify_dir = safe_opendir("/proc/self/fd");

    int verify_scan_fd = dirfd(verify_dir);
    if (verify_scan_fd == -1) {
        std::cerr << "Error: Failed to get directory FD for verification: " << strerror(errno) << "\n";
        closedir(verify_dir);
        throw std::runtime_error("Failed to get directory FD for verification");
    }

    errno = 0;
    while ((entry = safe_readdir(verify_dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char* endptr;
        long fd_num = strtol(entry->d_name, &endptr, 10);

        if (*endptr != '\0' || fd_num < 0) {
            continue;
        }

        // Validate fd_num is within int range before casting
        if (fd_num > INT_MAX) {
            std::cerr << "Warning: FD number " << fd_num << " exceeds INT_MAX in verification, skipping\n";
            continue;
        }

        int fd_as_int = static_cast<int>(fd_num);

        if (fd_as_int == verify_scan_fd) {
            continue;
        }

        // Check if this remaining FD is in the allowed list
        if (!std::binary_search(fd_allowed.begin(), fd_allowed.end(), fd_as_int)) {
            std::cerr << "Error: Unexpected FD " << fd_as_int << " remains open but is not in allowed list\n";
            closedir(verify_dir);
            throw std::runtime_error("Unexpected FD " + std::to_string(fd_as_int) + " remains open");
        }
    }

    // No need to check errno here since safe_readdir handles it

    if (closedir(verify_dir) != 0) {
        std::cerr << "Error: Failed to close verification directory: " << strerror(errno) << "\n";
        throw std::runtime_error("Failed to close verification directory");
    }

    std::cerr << "Successfully closed " << closed_count << " unwanted FDs\n";
    return closed_count;
}

} // namespace envcleaner

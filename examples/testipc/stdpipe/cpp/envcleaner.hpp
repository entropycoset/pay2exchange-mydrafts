#pragma once

#include <vector>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <dirent.h>

namespace envcleaner {

/**
 * @brief Variadic template function for validated libc calls
 *
 * This function wraps libc calls with proper error checking:
 * 1. Sets errno = 0 before the call
 * 2. Calls the function with forwarded arguments
 * 3. Uses validator to check if call was successful
 * 4. If validator returns false, prints errno and throws exception
 * 5. If validator returns true, returns the result
 *
 * @tparam TResult The return type of the function being called
 * @tparam TFunc The function type
 * @tparam TValidator The validator function type
 * @tparam TArgs The argument types
 * @param func The function to call
 * @param validator Function that takes TResult and returns bool (true = success)
 * @param args Arguments to forward to the function
 * @return TResult The result of the function call
 * @throws std::runtime_error if validator indicates failure
 */
template<typename TResult, typename TFunc, typename TValidator, typename... TArgs>
TResult validated_libc_call(TFunc func, TValidator validator, TArgs&&... args) {
		errno = 0;
		TResult result = func(std::forward<TArgs>(args)...);
		if (!validator(result)) {
				std::cerr << "Error: libc call failed with errno=" << errno << ": " << strerror(errno) << "\n";
				throw std::runtime_error("libc call failed: " + std::string(strerror(errno)));
		}
		return result;
}

/**
 * @brief Safe wrapper for readdir that uses validated_libc_call
 *
 * @param dir Directory stream pointer
 * @return struct dirent* Directory entry or nullptr at end (errno will be 0)
 * @throws std::runtime_error on read errors
 */
struct dirent* safe_readdir(DIR* dir);

/**
 * @brief Safe wrapper for opendir that uses validated_libc_call
 *
 * @param path Path to directory
 * @return DIR* Directory stream pointer
 * @throws std::runtime_error if directory cannot be opened
 */
DIR* safe_opendir(const char* path);

/**
 * @brief Safe wrapper for setenv that uses validated_libc_call
 *
 * @param name Environment variable name
 * @param value Environment variable value
 * @param overwrite Whether to overwrite existing variable
 * @throws std::runtime_error if setenv fails
 */
void safe_setenv(const char* name, const char* value, int overwrite);

/**
 * @brief Safe wrapper for unsetenv that uses validated_libc_call
 *
 * @param name Environment variable name to unset
 * @throws std::runtime_error if unsetenv fails
 */
void safe_unsetenv(const char* name);

/**
 * @brief Parse comma-separated list of integers safely
 *
 * @param input Comma-separated string like "1,2,3,4"
 * @return std::vector<int> Parsed integers
 * @throws std::runtime_error on parsing errors
 */
std::vector<int> parse_fd_list(const std::string& input);

/**
 * @brief Parse comma-separated list of strings safely
 *
 * @param input Comma-separated string like "PATH,USER,HOME"
 * @return std::vector<std::string> Parsed strings
 * @throws std::runtime_error on parsing errors
 */
std::vector<std::string> parse_string_list(const std::string& input);

/**
 * @brief Parse comma-separated list of key=value pairs safely
 *
 * @param input Comma-separated string like "HOME=/tmp,VAR=value"
 * @return std::vector<std::pair<std::string, std::string>> Parsed key-value pairs
 * @throws std::runtime_error on parsing errors
 */
std::vector<std::pair<std::string, std::string>> parse_env_pairs(const std::string& input);

/**
 * @brief Clean environment variables keeping only specified ones
 *
 * @param keep_vars Vector of environment variable names to keep
 * @throws std::runtime_error on errors
 */
void clean_environment(const std::vector<std::string>& keep_vars);

/**
 * @brief Set environment variables from key-value pairs
 *
 * @param env_vars Vector of key-value pairs to set
 * @throws std::runtime_error on errors
 */
void set_environment(const std::vector<std::pair<std::string, std::string>>& env_vars);

/**
 * @brief Count open file descriptors by scanning /proc/self/fd
 *
 * This function scans /proc/self/fd directory and counts all open file descriptors,
 * excluding the FD used for scanning itself. It throws exceptions on any errors.
 *
 * @return size_t Number of open file descriptors
 * @throws std::runtime_error on any scanning errors
 */
size_t count_open_fd();

/**
 * @brief Close all file descriptors except those in the allowed list
 *
 * This function:
 * 1. Deduplicates and sorts the allowed FDs vector
 * 2. Scans all open FDs and closes those not in the allowed list
 * 3. Verifies that the number of remaining FDs is <= size of allowed list
 * 4. Performs final verification that all remaining FDs are in allowed list
 *
 * @param fd_allowed Vector of file descriptor numbers to keep open
 * @return size_t Number of file descriptors that were closed
 * @throws std::runtime_error on any errors during scanning or closing
 */
size_t close_unwanted_fds(std::vector<int> fd_allowed);

} // namespace envcleaner

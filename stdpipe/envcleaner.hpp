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
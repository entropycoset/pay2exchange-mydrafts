#include "envcleaner.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

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

size_t count_open_fd() {
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
        
        // Skip the FD we're using to scan the directory
        if (fd_num == scan_fd) {
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
        
        if (fd_num == scan_fd) {
            continue;
        }
        
        open_fds.push_back(static_cast<int>(fd_num));
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
        
        if (fd_num == verify_scan_fd) {
            continue;
        }
        
        // Check if this remaining FD is in the allowed list
        if (!std::binary_search(fd_allowed.begin(), fd_allowed.end(), static_cast<int>(fd_num))) {
            std::cerr << "Error: Unexpected FD " << fd_num << " remains open but is not in allowed list\n";
            closedir(verify_dir);
            throw std::runtime_error("Unexpected FD " + std::to_string(fd_num) + " remains open");
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
#include "ecul.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace ecul {

namespace {
    // Helper function to get current timestamp
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
    
    // Helper function to format log message
    std::string format_log_message(const std::string& level, const std::string& message, const codeplace& location) {
        std::ostringstream oss;
        oss << "[" << get_timestamp() << "] "
            << "[" << level << "] "
            << "[" << location.to_string() << "] "
            << message;
        return oss.str();
    }
}

void log_abort(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("ABORT", message, location);
    std::cerr << formatted << std::endl;
    std::cerr << "CRITICAL: Program will abort immediately due to unrecoverable error!" << std::endl;
}

void log_stop(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("STOP", message, location);
    std::cerr << formatted << std::endl;
    std::cerr << "CRITICAL: Security/logic violation - program must stop execution!" << std::endl;
    std::cerr << "WARNING: Throwing critical_do_not_catch_exception_stop - DO NOT CATCH THIS!" << std::endl;
}

void log_erro(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("ERROR", message, location);
    std::cerr << formatted << std::endl;
}

void log_warn(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("WARN", message, location);
    std::cerr << formatted << std::endl;
}

void log_info(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("INFO", message, location);
    std::cout << formatted << std::endl;
}

critical_do_not_catch_exception_stop create_stop_exception(const std::string& message, const codeplace& location) {
    log_stop(message, location);
    return critical_do_not_catch_exception_stop(message, location);
}

} // namespace ecul
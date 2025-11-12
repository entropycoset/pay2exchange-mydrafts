#include "ecul.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <atomic>
#include <mutex>

namespace ecul {

namespace {
    // Color detection and formatting - thread-safe globals
    std::atomic<bool> colors_initialized{false};
    std::atomic<bool> colors_supported{false};

    std::string fg_code_for(int idx) {
        if (idx < 0) return "";
        if (idx <= 7) return std::to_string(30 + idx);
        return std::to_string(90 + (idx - 8));
    }

    std::string bg_code_for(int idx) {
        if (idx < 0) return "";
        if (idx <= 7) return std::to_string(40 + idx);
        return std::to_string(100 + (idx - 8));
    }

    std::string format_color_internal(const std::string& txt, int fg, int bg) {
        // CRITICAL: Only use colors if explicitly initialized by user from main()
        // This ensures no color codes are used during early startup or pre-main errors
        if (!colors_initialized.load()) {
            return txt;  // Return plain text if colors not initialized
        }

        if (!colors_supported.load()) {
            return txt;  // Return plain text if colors not supported
        }

        std::ostringstream seq;
        bool has_codes = false;

        if (fg >= 0) {
            seq << fg_code_for(fg);
            has_codes = true;
        }
        if (bg >= 0) {
            if (has_codes) seq << ";";
            seq << bg_code_for(bg);
            has_codes = true;
        }

        if (!has_codes) return txt;
        return "\033[" + seq.str() + "m" + txt + "\033[0m";
    }

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
}

// Color initialization function - MUST be called from main() before using colors
void init_colors() {
    // Thread-safe singleton pattern using static local variable
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        // Detect color support: check if stderr is a tty
        bool is_tty = isatty(STDERR_FILENO);
        colors_supported.store(is_tty);
        colors_initialized.store(true);
    });
}

// Check if colors were initialized
bool is_colors_initialized() {
    return colors_initialized.load();
}

// Color support function
std::string colortxt(const std::string& txt, Color fg, Color bg) {
    int fg_val = (fg == Color::Default || fg == Color::Reset || fg == Color::Normal) ? -1 : static_cast<int>(fg);
    int bg_val = (bg == Color::Default || bg == Color::Reset || bg == Color::Normal) ? -1 : static_cast<int>(bg);
    return format_color_internal(txt, fg_val, bg_val);
}

// Project name detection using ODR fallback
__attribute__((weak)) std::string get_project_name() {
    // This function will be overridden by ODR if user defines a stronger version
    // This is the weak fallback implementation
    return "unknown";
}

// Binary name detection with caching
std::string get_binary_name() {
    static std::string cached_name;
    static bool name_initialized = false;

    if (name_initialized) return cached_name;
    name_initialized = true;

    try {
        // Read symlink target
        char buffer[1024];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1) {
            cached_name = "unknown";
            return cached_name;
        }

        buffer[len] = '\0';
        std::string full_path(buffer);

        // Extract just the filename
        size_t pos = full_path.find_last_of("/\\");
        if (pos != std::string::npos) {
            cached_name = full_path.substr(pos + 1);
        } else {
            cached_name = full_path;
        }
    } catch (...) {
        cached_name = "unknown";
    }

    return cached_name;
}

// Get project and binary prefix
std::string get_project_binary_prefix() {
    return "{" + get_binary_name() + ", " + get_project_name() + "}";
}

namespace {
    // Helper function to format log message with project/binary prefix
    std::string format_log_message(const std::string& level, const std::string& message,
                                 const codeplace& location) {
        std::ostringstream oss;
        oss << get_project_binary_prefix() << " "
            << "[" << get_timestamp() << "] "
            << "[" << level << "] "
            << "[" << location.to_string() << "] "
            << message;
        return oss.str();
    }

    // Helper to output both plain and colored versions for critical messages
    void output_critical_message(const std::string& plain_msg, const std::string& colored_msg) {
        // First output plain to both streams
        std::cout << plain_msg << std::endl;
        std::cerr << plain_msg << std::endl;
        std::cout.flush();
        std::cerr.flush();

        // Then output colored version
        std::cout << colored_msg << std::endl;
        std::cerr << colored_msg << std::endl;
        std::cout.flush();
        std::cerr.flush();
    }
}

void log_abort(const std::string& message, const codeplace& location) {
    std::string plain_msg = format_log_message("ABORT", message, location);

    // Create colored version with critical error prefix
    std::string critical_prefix = colortxt("CRITICAL ERROR! will abort program!", Color::Black, Color::Red);
    std::string colored_body = colortxt(format_log_message("ABORT", message, location),
                                      Color::Red, Color::LightYellow);
    std::string colored_msg = critical_prefix + " " + colored_body;

    output_critical_message(plain_msg, colored_msg);
}

void log_stop(const std::string& message, const codeplace& location) {
    std::string plain_msg = format_log_message("STOP", message, location);

    // Create colored version with stop error prefix
    std::string stop_prefix = colortxt("STOP-ERROR, will soon stop program or part...",
                                     Color::Magenta, Color::LightYellow);
    std::string colored_body = colortxt(format_log_message("STOP", message, location),
                                      Color::Magenta, Color::LightYellow);
    std::string colored_msg = stop_prefix + " " + colored_body;

    output_critical_message(plain_msg, colored_msg);
}

void log_erro(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("ERRO", message, location);
    std::string colored = colortxt(formatted, Color::Red, Color::Black);
    std::cerr << colored << std::endl;
    std::cerr.flush();
}

void log_warn(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("warn", message, location);
    std::string colored = colortxt(formatted, Color::Yellow, Color::Black);
    std::cerr << colored << std::endl;
    std::cerr.flush();
}

void log_info(const std::string& message, const codeplace& location) {
    std::string formatted = format_log_message("info", message, location);
    std::string colored = colortxt(formatted, Color::Blue, Color::Black);
    std::cerr << colored << std::endl;
    std::cerr.flush();
}

critical_do_not_catch_exception_stop create_stop_exception(const std::string& message, const codeplace& location) {
    log_stop(message, location);
    return critical_do_not_catch_exception_stop(message, location);
}

} // namespace ecul
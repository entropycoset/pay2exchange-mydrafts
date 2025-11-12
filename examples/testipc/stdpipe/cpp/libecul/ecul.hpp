#ifndef ECUL_HPP
#define ECUL_HPP

#include <string>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <type_traits>
#include <string_view>

// ODR fallback for project name detection - user should define this in their code
// If not defined, get_project_name() will return "unknown"

namespace ecul {

// Color support (integrated from libvalidcolor)
enum class Color : int {
    // Basic colors (0-7)
    Black = 0, Red = 1, Green = 2, Yellow = 3,
    Blue = 4, Magenta = 5, Cyan = 6, White = 7,
    // Light/Bright colors (8-15)
    LightBlack = 8, LightRed = 9, LightGreen = 10, LightYellow = 11,
    LightBlue = 12, LightMagenta = 13, LightCyan = 14, LightWhite = 15,
    // Special values
    Default = -1, Reset = -2, Normal = -1
};

// Color initialization and text wrapper functions
void init_colors();  // Must be called from main() before using colors
bool is_colors_initialized();
std::string colortxt(const std::string& txt, Color fg = Color::Default, Color bg = Color::Default);

// Project and binary identification functions
std::string get_project_name();
std::string get_binary_name();
std::string get_project_binary_prefix();

// Code location structure
struct codeplace {
    const char* file;
    int line;

    codeplace(const char* f, int l) : file(f), line(l) {}

    std::string to_string() const {
        std::string filename = file;
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }
        return filename + ":" + std::to_string(line);
    }
};

// Special exception that should NEVER be caught (like stop_exception from STYLEGUIDE.txt)
class critical_do_not_catch_exception_stop final {
private:
    std::string m_message;
    codeplace m_location;

public:
    critical_do_not_catch_exception_stop(const std::string& msg, const codeplace& loc)
        : m_message(msg), m_location(loc) {}

    const char* what() const noexcept { return m_message.c_str(); }
    const codeplace& where() const noexcept { return m_location; }
};

// Enhanced logging functions with colors and project identification
void log_abort(const std::string& message, const codeplace& location);
void log_stop(const std::string& message, const codeplace& location);
void log_erro(const std::string& message, const codeplace& location);
void log_warn(const std::string& message, const codeplace& location);
void log_info(const std::string& message, const codeplace& location);

// Helper function to create stop exception
[[nodiscard]] critical_do_not_catch_exception_stop create_stop_exception(const std::string& message, const codeplace& location);

// Template helper to log exception.what() if available
template<typename T>
std::string extract_what_if_available(const T& exception) {
    if constexpr (std::is_base_of_v<std::exception, T>) {
        return std::string(exception.what());
    } else {
        return "[exception type has no .what() method]";
    }
}

// Helper function for ecul_erro_what
template<typename ExceptionType>
[[nodiscard]] ExceptionType create_logged_exception(ExceptionType&& exception, const codeplace& location) {
    static_assert(std::is_base_of_v<std::exception, std::decay_t<ExceptionType>>,
                  "Exception must inherit from std::exception");

    std::string what_msg = extract_what_if_available(exception);
    log_erro(what_msg, location);
    return std::forward<ExceptionType>(exception);
}

// Helper function for ecul_erro_msg
template<typename ExceptionType>
[[nodiscard]] ExceptionType create_logged_exception_with_msg(const std::string& custom_msg, ExceptionType&& exception, const codeplace& location) {
    static_assert(std::is_base_of_v<std::exception, std::decay_t<ExceptionType>>,
                  "Exception must inherit from std::exception");

    std::string what_msg = extract_what_if_available(exception);
    std::string full_msg = custom_msg;
    if (!what_msg.empty() && what_msg != "[exception type has no .what() method]") {
        full_msg += " [exception.what(): " + what_msg + "]";
    }
    log_erro(full_msg, location);
    return std::forward<ExceptionType>(exception);
}

} // namespace ecul

// Macro to get current code location
#define ECUL_HERE() ecul::codeplace(__FILE__, __LINE__)

// Basic logging macros
#define ecul_log_abort(msg) ecul::log_abort((msg), ECUL_HERE())
#define ecul_log_stop(msg) ecul::log_stop((msg), ECUL_HERE())
#define ecul_log_erro(msg) ecul::log_erro((msg), ECUL_HERE())
#define ecul_log_warn(msg) ecul::log_warn((msg), ECUL_HERE())
#define ecul_log_info(msg) ecul::log_info((msg), ECUL_HERE())

// Action macros
/// Call when abort-error occurred (critical, program is damaged, needs to terminate now).
/// Program will log and abort here (function/macro does not return).
#define ecul_abort(msg) do { ecul_log_abort(msg); std::abort(); } while(0)

/// When a stop-error occurred (one that means program should not resume normal operation)
/// then run as: throw ecul_stop(msg). It will throw exception
/// ecul::critical_do_not_catch_exception_stop that should travel to top of main or other
/// specialized place, as checked by linters if they apply.
#define ecul_stop(msg) ecul::create_stop_exception((msg), ECUL_HERE())

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_runtime(msg)` - it will log error, and you will throw it.
#define ecul_erro_runtime(msg) std::runtime_error(msg); ecul_log_erro(msg)

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_what(your_exception(msg...))` - where your_exception(...) forms any
/// expression compatible with .what() - it will log error from .what of your object,
/// and you will throw it.
#define ecul_erro_what(exception) ecul::create_logged_exception((exception), ECUL_HERE())

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_msg(msg, your_exception(...))` - where your_exception(...) forms any
/// expression - it will log error from msg, and if possible also from the .what() of
/// your object, and you will throw it.
#define ecul_erro_msg(msg, exception) ecul::create_logged_exception_with_msg((msg), (exception), ECUL_HERE())

#define ecul_warn(msg) ecul_log_warn(msg) ///< Just logs a warning, code continues
#define ecul_info(msg) ecul_log_info(msg) ///< Simply logs an information

// Static assertion to prevent direct usage without throw (compile-time check)
// This creates a more forceful error than just [[nodiscard]]
#define ECUL_MUST_THROW_CHECK(expr) \
    static_assert(std::is_same_v<decltype(expr), ecul::critical_do_not_catch_exception_stop>, \
                  "ecul_stop() result must be used with 'throw' statement")

#endif // ECUL_HPP
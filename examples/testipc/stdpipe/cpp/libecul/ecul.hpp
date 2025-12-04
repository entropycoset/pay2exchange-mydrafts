#ifndef ECUL_HPP
#define ECUL_HPP

#include <string>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <type_traits>
#include <string_view>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>

// ODR fallback for project name detection - user should define this in their code
// If not defined, get_project_name() will return "unknown"
// TODO move this^

namespace ecul {

/// @example someprint( mkstr() << a << 123 << "foo" << std::setw(10) << c );
class mkstr final {
    std::ostringstream oss;
public:
    mkstr() = default;

    // do not copy/move, just use in place in one expression (that is the intended use)
    mkstr(const mkstr&) = delete;
    mkstr(mkstr&&) = delete;
    mkstr& operator=(const mkstr&) = delete;
    mkstr& operator=(mkstr&&) = delete;

    template<typename T>
    mkstr& operator<<(const T& value) {
        oss << value;
        return *this;
    }

    mkstr& operator<<(std::ostream& (*manip)(std::ostream&));
    operator std::string() const; ///< should decay to a string object faciliating the intended use as expression print(mkstr()<<....<<....)
};


namespace detail {
    inline thread_local std::mt19937 thread_local_pseudorandom{std::random_device{}()};
}

template <typename T>
T safe_pseudorandom(T min, T max) {
    if (min > max) {
        throw std::invalid_argument("safe_pseudorandom: min > max");
    }
    std::uniform_int_distribution<T> dist(min, max);
    return dist(detail::thread_local_pseudorandom);
}

/// Some memory that can be dropped in parts to regain memory for emergency. Can be then restored.
/// Aborts (std::abort) in case of some of serious internal problems (e.g. program out of ram despite this)
/// No special guarantees (not thread-safe, not concerend with SIOF, though doesn't use static data itself so should work before and after main if lifetime of this would allow)
class MemBallast {
protected:
    bool emergency_free_tiny_or_abort(MemBallast *tiny_ballast) noexcept; ///< free my internal/tiny ballast (or abort if can't)
    std::ostream &log_err(); ///< start a log line for internal error, goes to cerr

public:
    MemBallast(MemBallast *tiny_ballast, std::size_t bufferSize, std::size_t bufferCount, std::size_t buffMinimal);
    virtual ~MemBallast();

    /// Free just one buffer (last one) if available, return true if a buffer got freed now
    bool emergency_free() noexcept;

    // Again allocate buffers up to reasonable level, abort if not possible to achieve reasonable level
    bool safe_rearm_or_abort() noexcept;

    // Again allocate buffers up to the full condition (if possible?). Return: did we allocated all we wanted
    bool safe_rearm_as_much() noexcept;
    void clear_all_locked() noexcept; ///< quickly free all memory that we can, usually because we are about to terminate

private:
    MemBallast * m_tiny_ballast; ///< my "parent" (tiny) ballast, to use when I myself encounter problems and want to report internal problems (eg last problems with low memory)
    std::size_t bufferSize_; ///< size of each buffer
    std::size_t bufferCount_, buffMinimal_; ///< the expected amount of buffers, and the minimal amount before we decide memory is critically low still now
    std::vector<char*> buffers_;
};

/// this class must be created just once (vs SIOF) - use instance pattern
/// functions here are thread-safe because of integral mutex m_mtx. as for SIOF: does no special protection but also does not use static data.
/// aiming to be safe everywhere if accessed via singleton
class MemBallastComplete {
protected:
    MemBallast *m_normal, *m_tiny;
    std::mutex m_mtx; ///< hold this mutex when accesing m_normal, m_tiny (and in dtor)

    void free_all_we_will_die(); ///<  (thread-safe, internal mutex) free all we can (caller then should report problem and abort)

public:
    MemBallastComplete();
    virtual ~MemBallastComplete();

    void free_some_memory(); ///< (thread-safe, internal mutex) increase amount of free memory for emergencies (please later call repair)
    void we_are_safe(); ///< (thread-safe, internal mutex) all is ok now to repair (again allocate ballast, if any was used up)
};

/// top level API to use memory emergency - should be able to call these static functions from any place also before/after main (SIOF-safe) and from threads
/// due to it using instance_of_ballast to init the object when needed
struct MemEmergencySys final {
private:
    static MemBallastComplete & instance_of_ballast();
public:
    static void free_some_memory(); ///< increase amount of free memory for emergencies (please later call repair)
    static void we_are_safe(); ///< all is ok now to repair (again allocate ballast, if any was used up)
};



// TODO merge this with libvalidcolora (include that lib? but make it atomic?)
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

// Logging settings enums
enum class DateFormat {
    long_date,    // YYYY-MM-DD (as current)
    no_date       // no date shown
};

enum class TimeFormat {
    with_sub,     // HH:MM:SS.mmm (as current)
    normal,       // HH:MM:SS
    short_time,   // HH:MM (renamed from 'short' to avoid keyword conflict)
    none          // no time shown
};

enum class RuntimeFormat {
    none,         // no runtime shown
    seconds,      // show seconds since start
    ms,           // show milliseconds .000 to .999
    high          // show 6 digits precision sub-second
};

enum class ProgramNameFormat {
    prefer_name,  // show configured manual name, fallback to prefer_bin
    prefer_bin,   // show bin name from /proc/, fallback to prefer_name
    both          // show both if possible
};

enum class SpacingFormat {
    wide,         // as current (spaces between all components)
    normal,       // skip spaces between major components
    compact       // also skip spaces inside components
};

// Global logging settings class with thread-safe access
class LogSettings {
private:
	mutable std::mutex settings_mutex;
	DateFormat date_format = DateFormat::long_date;
	TimeFormat time_format = TimeFormat::with_sub;
	RuntimeFormat runtime_format = RuntimeFormat::none;
	ProgramNameFormat program_name_format = ProgramNameFormat::prefer_name;
	int line_width = -1; // -1 means no setw
	SpacingFormat spacing_format = SpacingFormat::wide;
	std::chrono::steady_clock::time_point program_start_time = std::chrono::steady_clock::now();
	
	// Program icon settings
	std::string program_icon = "";
	bool program_icon_usecolor = false;
	int program_icon_fg = static_cast<int>(Color::Default);
	int program_icon_bg = static_cast<int>(Color::Default);
	bool program_icon_show = false;

public:
    // Getters with mutex protection
    DateFormat get_date_format() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return date_format;
    }
    
    TimeFormat get_time_format() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return time_format;
    }

    RuntimeFormat get_runtime_format() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return runtime_format;
    }

    ProgramNameFormat get_program_name_format() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return program_name_format;
    }

    int get_line_width() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return line_width;
    }

    SpacingFormat get_spacing_format() const {
        std::lock_guard<std::mutex> lock(settings_mutex);
        return spacing_format;
    }

    std::chrono::steady_clock::time_point get_program_start_time() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_start_time;
    }
   
    // Program icon getters
    std::string get_program_icon() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_icon;
    }
   
    bool get_program_icon_usecolor() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_icon_usecolor;
    }
   
    int get_program_icon_fg() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_icon_fg;
    }
   
    int get_program_icon_bg() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_icon_bg;
    }
   
    bool get_program_icon_show() const {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	return program_icon_show;
    }
   
    // Setters with mutex protection
    void set_date_format(DateFormat format) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	date_format = format;
    }

    void set_time_format(TimeFormat format) {
        std::lock_guard<std::mutex> lock(settings_mutex);
        time_format = format;
    }

    void set_runtime_format(RuntimeFormat format) {
        std::lock_guard<std::mutex> lock(settings_mutex);
        runtime_format = format;
    }

    void set_program_name_format(ProgramNameFormat format) {
        std::lock_guard<std::mutex> lock(settings_mutex);
        program_name_format = format;
    }

    void set_line_width(int width) {
        std::lock_guard<std::mutex> lock(settings_mutex);
        line_width = width;
    }

    void set_spacing_format(SpacingFormat format) {
        std::lock_guard<std::mutex> lock(settings_mutex);
        spacing_format = format;
    }

    // Program icon setters
    void set_program_icon(const std::string& icon) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_icon = icon;
    }
   
    void set_program_icon_usecolor(bool use_color) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_icon_usecolor = use_color;
    }
   
    void set_program_icon_fg(int fg) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_icon_fg = fg;
    }
   
    void set_program_icon_bg(int bg) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_icon_bg = bg;
    }
   
    void set_program_icon_show(bool show) {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_icon_show = show;
    }
   
    // Reset program start time (called at program start)
    void reset_program_start_time() {
    	std::lock_guard<std::mutex> lock(settings_mutex);
    	program_start_time = std::chrono::steady_clock::now();
    }
};

// Global settings instance
LogSettings& get_log_settings();

// Initialize logging settings with program start time
void init_logging_settings();

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
/// @usage: ecul_abort("Out of mem."); // program will terminate here
#define ecul_abort(msg) do { ecul::MemEmergencySys::free_some_memory(); ecul_log_abort(msg); std::abort(); } while(0)

/// This expression creates stop-error you must immediatelly throw it, this should be used in case of stop-error
/// @usage: throw ecul_stop(msg).
/// (that is: an error that means program should not resume normal operation, but should quit soon, usually be propagating this exception to top of main()).
/// It will throw exception
/// ecul::critical_do_not_catch_exception_stop that should travel to top of main or other
/// specialized place, as checked by linters if they apply.
/// if you do catch this special exception with special catch for it then there return the memory to ecul::MemEmergencySys
#define ecul_stop(msg) ( (ecul::MemEmergencySys::free_some_memory()) , ecul_log_stop(msg) , ecul::create_stop_exception((msg), ECUL_HERE()) )

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_runtime(msg)` - it will log error, and you will throw it.
#define ecul_erro_runtime(msg) ( ecul_log_erro(msg) , std::runtime_error(msg) )

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_what(your_exception(msg...))` - where your_exception(...) forms any
/// expression compatible with .what() - it will log error from .what of your object,
/// and you will throw it.
#define ecul_erro_what(exception) ( ecul::create_logged_exception((exception), ECUL_HERE()) )

/// For normal errors that result in a throw - if such error occurs then do expression
/// `throw ecul_erro_msg(msg, your_exception(...))` - where your_exception(...) forms any
/// expression - it will log error from msg, and if possible also from the .what() of
/// your object, and you will throw it.
#define ecul_erro_msg(msg, exception) ( ecul::create_logged_exception_with_msg((msg), (exception), ECUL_HERE()) )

#define ecul_warn(msg) ecul_log_warn(msg) ///< Just logs a warning, code continues
#define ecul_info(msg) ecul_log_info(msg) ///< Simply logs an information

// Static assertion to prevent direct usage without throw (compile-time check)
// This creates a more forceful error than just [[nodiscard]]
#define ECUL_MUST_THROW_CHECK(expr) \
    static_assert(std::is_same_v<decltype(expr), ecul::critical_do_not_catch_exception_stop>, \
                  "ecul_stop() result must be used with 'throw' statement")

#endif // ECUL_HPP

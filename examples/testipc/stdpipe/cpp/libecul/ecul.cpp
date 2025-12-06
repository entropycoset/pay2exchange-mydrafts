#include "ecul.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iomanip>


namespace ecul {

// ---

mkstr& mkstr::operator<<(std::ostream& (*manip)(std::ostream&)) {
    oss << manip;
    return *this;
}

mkstr::operator std::string() const { return oss.str(); }

// Global settings instance with thread-safe singleton pattern
LogSettings& get_log_settings() {
    static LogSettings instance;
    return instance;
}

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

    // Helper function to format timestamp according to settings
    std::string get_formatted_timestamp(const LogSettings& settings) {
        std::ostringstream oss;
        const auto spacing = (settings.get_spacing_format() == SpacingFormat::compact) ? "" : " ";

        // Date part
        if (settings.get_date_format() == DateFormat::long_date) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d");

            // Add space between date and time if both are shown
            if (settings.get_time_format() != TimeFormat::none) {
                oss << spacing;
            }
        }

        // Time part
        if (settings.get_time_format() != TimeFormat::none) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);

            switch (settings.get_time_format()) {
                case TimeFormat::with_sub: {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;
                    oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
                    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
                    break;
                }
                case TimeFormat::normal:
                    oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
                    break;
                case TimeFormat::short_time:
                    oss << std::put_time(std::localtime(&time_t), "%H:%M");
                    break;
                case TimeFormat::none:
                    // Already handled above
                    break;
            }
        }

        return oss.str();
    }

    // Helper function to format runtime according to settings
    std::string get_formatted_runtime(const LogSettings& settings) {
        if (settings.get_runtime_format() == RuntimeFormat::none) {
            return "";
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - settings.get_program_start_time();

        std::ostringstream oss;
        switch (settings.get_runtime_format()) {
            case RuntimeFormat::seconds: {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
                oss << secs.count() << "s";
                break;
            }
            case RuntimeFormat::ms: {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
                auto secs = ms.count() / 1000;
                auto remainder_ms = ms.count() % 1000;
                oss << secs << '.' << std::setfill('0') << std::setw(3) << remainder_ms;
                break;
            }
            case RuntimeFormat::high: {
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
                auto secs = us.count() / 1000000;
                auto remainder_us = us.count() % 1000000;
                oss << secs << '.' << std::setfill('0') << std::setw(6) << remainder_us;
                break;
            }
            case RuntimeFormat::none:
                // Already handled above
                break;
        }
        return oss.str();
    }

    // Helper function to format program name according to settings
    std::string get_formatted_program_name(const LogSettings& settings) {
        auto format = settings.get_program_name_format();
        const auto spacing = (settings.get_spacing_format() == SpacingFormat::compact) ? "," : ", ";

        switch (format) {
            case ProgramNameFormat::prefer_name: {
                std::string project_name = get_project_name();
                if (project_name != "unknown") {
                    return project_name;
                } else {
                    return get_binary_name();
                }
            }
            case ProgramNameFormat::prefer_bin: {
                std::string bin_name = get_binary_name();
                if (bin_name != "unknown") {
                    return bin_name;
                } else {
                    return get_project_name();
                }
            }
            case ProgramNameFormat::both: {
                std::string bin_name = get_binary_name();
                std::string project_name = get_project_name();
                if (bin_name != "unknown" && project_name != "unknown") {
                    return bin_name + spacing + project_name;
                } else if (bin_name != "unknown") {
                    return bin_name;
                } else {
                    return project_name;
                }
            }
        }
        return "unknown";
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

        // Initialize logging settings with program start time
        init_logging_settings();
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
    }
    // UNSAFE_LINTER_IGNORE_CATCH_ALL
    catch (...) {
        cached_name = "unknown";
    }

    return cached_name;
}

// Initialize logging settings with program start time
void init_logging_settings() {
    auto& settings = get_log_settings();
    settings.reset_program_start_time();
}

namespace {
    // Helper function to format log message with configurable formatting
    std::string format_log_message(const std::string& level, const std::string& message,
                                 const codeplace& location) {
        const auto& settings = get_log_settings();
        std::ostringstream oss;
        
        // Apply line width setting if specified
        if (settings.get_line_width() > 0) {
            oss << std::setw(settings.get_line_width());
        }

        // Determine spacing based on settings
        const auto major_space = (settings.get_spacing_format() == SpacingFormat::normal ||
                                 settings.get_spacing_format() == SpacingFormat::compact) ? "" : " ";

        // Program name part
        std::string program_part = "{" + get_formatted_program_name(settings) + "}";
        oss << program_part;

        // Timestamp part
        std::string timestamp = get_formatted_timestamp(settings);
        if (!timestamp.empty()) {
            oss << major_space << "[" << timestamp << "]";
        }

        // Runtime part
        std::string runtime = get_formatted_runtime(settings);
        if (!runtime.empty()) {
            oss << major_space << "[" << runtime << "]";
        }

        // Level part
        oss << major_space << "[" << level << "]";

        // Program icon part - place before location if enabled
        if (settings.get_program_icon_show()) {
            std::string icon_text = settings.get_program_icon();
            if (!icon_text.empty()) {
                // Format icon with brackets and optional colors
                std::string formatted_icon = "[" + icon_text + "]";
                if (settings.get_program_icon_usecolor()) {
                    formatted_icon = format_color_internal(formatted_icon,
                                                         settings.get_program_icon_fg(),
                                                         settings.get_program_icon_bg());
                }
                oss << major_space << formatted_icon;
            }
        }

        // Location part
        oss << major_space << "[" << location.to_string() << "]";

        // Message part
        oss << major_space << message;

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

// ---

bool MemBallast::emergency_free_tiny_or_abort(MemBallast *tiny_ballast) noexcept {
    if (!tiny_ballast) {
        log_err() << "I don't have tiny-ballast to use here (null) - will abort now!" << std::endl;
        std::abort(); // UNSAFE_LINTER_IGNORE_ABORT_CALL
    }
    tiny_ballast->emergency_free();

    return true; //TODO check if we managed to drop someting there
}

std::ostream & MemBallast::log_err() {
    std::ostream &out = std::cerr;
    out << "ERROR: In MemBallast@" << (static_cast<void*>(this)) << ": ";
    return out;
}

MemBallast::MemBallast(MemBallast *tiny_ballast, std::size_t bufferSize, std::size_t bufferCount, std::size_t buffMinimal)
    : m_tiny_ballast(tiny_ballast), bufferSize_(bufferSize), bufferCount_(bufferCount), buffMinimal_(buffMinimal)
{ }

bool MemBallast::emergency_free() noexcept {
    try {
        if (!buffers_.empty()) {
            delete[] buffers_.back();
            buffers_.pop_back();
            buffers_.shrink_to_fit();
        }
    } catch(const std::runtime_error &exc) { // probably not possible
        this->emergency_free_tiny_or_abort(m_tiny_ballast);
        log_err() << "some exception during emergency_free() : " << exc.what();
        return false; // !
    }
    return true;
}

// Again allocate buffers up to reasonable level, abort if not possible to achieve reasonable level
bool MemBallast::safe_rearm_or_abort() noexcept {
    this->safe_rearm_as_much();
    auto const current = buffers_.size();
    if (current >= this->bufferCount_) return true; // very good
    if (current >= this->buffMinimal_) return false; // not so great, but we can continue
    { // else - we can't get any reasonable amount
        this->clear_all_locked();
        log_err() << "Can not restore ballast to even minimal safe level "
                  << "need " << this->buffMinimal_ << " or better " << this->bufferCount_
                  << "but got just " << current << "."
                  << " - will abort now!" << std::flush;
        std::abort(); // UNSAFE_LINTER_IGNORE_ABORT_CALL
    }
}

// Again allocate buffers up to the full condition (if possible?). Return: did we allocated all we wanted
bool MemBallast::safe_rearm_as_much() noexcept {
    try {
        try {
            buffers_.reserve(bufferCount_);
        } catch (std::bad_alloc &) {
            this->emergency_free_tiny_or_abort(m_tiny_ballast);
            log_err() << "Warning: totally out of memory (in MemBallast, reserve)\n" << std::flush;
            return false;
        }

        for (std::size_t i = buffers_.size(); i < bufferCount_; ++i) {
            char* block = new (std::nothrow) char[bufferSize_];
            if (!block) {
                /*
                this->emergency_free_tiny_or_abort(m_tiny_ballast);
                std::cerr << "Warning: out of memory (in MemBallast, new block)\n" << std::flush;
                std::cerr << "Warning: out of memory: MemBallast failed to allocate buffer index="
                          << i << " of size " << bufferSize_ << " bytes.\n";
                */
                return false; // !
            }
            buffers_.push_back(block);
        }
    } catch (std::runtime_error &exc) {
        this->emergency_free_tiny_or_abort(m_tiny_ballast);
        log_err() << "Warning: memory: in balast: problems (in MemBallast, general)\n" << std::flush;
        log_err() << "Warning: memory: in balast: rearm got exception: " << std::flush << exc.what() << std::endl;
        return false; // !
    }
    return true;
}

MemBallast::~MemBallast() {
    clear_all_locked();
}

void MemBallast::clear_all_locked() noexcept {
    for (char* block : buffers_) {
        delete[] block;
    }
    buffers_.clear();
    buffers_.shrink_to_fit();
}

// ---

MemBallastComplete & MemEmergencySys::instance_of_ballast() {
    static MemBallastComplete obj; // safe init (thread-safe and SIOF-safe)
    return obj;
}

void MemEmergencySys::free_some_memory() {
    instance_of_ballast().free_some_memory();
}

void MemEmergencySys::we_are_safe() {
    instance_of_ballast().we_are_safe();
}

// ---

MemBallastComplete::MemBallastComplete() {
    try {
        m_tiny = new MemBallast(nullptr, 16*1024, 32, 24);
        m_normal = new MemBallast(m_tiny, 64*1024, 16, 8);
    }
    catch (const std::runtime_error & exc) {
        std::cerr << "ERROR: low memory? Failed to even init the MemBallastComplete - will abort now!" << std::endl;
        std::abort(); // UNSAFE_LINTER_IGNORE_ABORT_CALL
    }
    bool rearm_ok = [this]() -> bool {
        if (! m_tiny->safe_rearm_or_abort()) return false;
        if (! m_normal->safe_rearm_or_abort()) return false;
        return true;
    }() ;
    if (!rearm_ok) {
        m_normal->emergency_free();
        // a bit dead code it probably aborted above in this case _or_abort()
        std::cerr << "ERROR: low memory? Failed to arm the MemBallastComplete - on ctor - will abort now!" << std::endl;
        std::abort(); // UNSAFE_LINTER_IGNORE_ABORT_CALL
    }
}

MemBallastComplete::~MemBallastComplete() {
    // wold lock_guard here if not empty.

    // on purpose do NOT free any memory, program is ending now anyway
}

void MemBallastComplete::free_some_memory() {
    std::lock_guard<std::mutex> lg(this->m_mtx);
    m_normal->emergency_free();
}

void MemBallastComplete::free_all_we_will_die() {
    std::lock_guard<std::mutex> lg(this->m_mtx);
    m_normal->clear_all_locked();
    m_tiny->clear_all_locked();
    // not deleting the pointer-vars on purpose (avoid potential nullptr?)
}

void MemBallastComplete::we_are_safe() {
    std::lock_guard<std::mutex> lg(this->m_mtx);
    bool rearm_ok = [this]() -> bool {
        if (! m_tiny->safe_rearm_or_abort()) return false;
        if (! m_normal->safe_rearm_or_abort()) return false;
        return true;
    }() ;
    if (!rearm_ok) {
        m_normal->emergency_free();
        std::cerr << "ERROR: low memory? Failed to arm the MemBallastComplete - in runtime - will abort now!" << std::endl;
        std::abort(); // UNSAFE_LINTER_IGNORE_ABORT_CALL
    }
}

// ---


} // namespace ecul

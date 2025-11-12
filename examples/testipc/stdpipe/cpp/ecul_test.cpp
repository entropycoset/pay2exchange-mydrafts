#include "libecul/ecul.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// Override the project name function for this test
namespace ecul {
    std::string get_project_name() {
        return "EculTestApp";
    }
}

// Test functions
void test_color_initialization() {
    std::cout << "=== Test: Color Initialization ===" << std::endl;
    std::cout << "Colors initialized before init: " << (ecul::is_colors_initialized() ? "YES" : "NO") << std::endl;
    ecul::init_colors();
    std::cout << "Colors initialized after init: " << (ecul::is_colors_initialized() ? "YES" : "NO") << std::endl;
    std::cout << "Color initialization test completed.\n" << std::endl;
}

void test_basic_logging() {
    std::cout << "=== Test: Basic Logging ===" << std::endl;
    ecul_info("This is an informational message");
    ecul_warn("This is a warning message");
    ecul_log_erro("This is an error message (logged but no exception)");
    std::cout << "Basic logging test completed.\n" << std::endl;
}

void test_project_identification() {
    std::cout << "=== Test: Project Identification ===" << std::endl;
    std::cout << "Project name: " << ecul::get_project_name() << std::endl;
    std::cout << "Binary name: " << ecul::get_binary_name() << std::endl;
    std::cout << "Full prefix: {" << ecul::get_binary_name() << ", " << ecul::get_project_name() << "}" << std::endl;
    std::cout << "Project identification test completed.\n" << std::endl;
}

void test_color_support() {
    std::cout << "=== Test: Color Support ===" << std::endl;
    std::cout << "Testing color formatting:" << std::endl;
    std::cout << ecul::colortxt("Red text on yellow background", ecul::Color::Red, ecul::Color::Yellow) << std::endl;
    std::cout << ecul::colortxt("Blue text on default background", ecul::Color::Blue) << std::endl;
    std::cout << ecul::colortxt("Green text on black background", ecul::Color::Green, ecul::Color::Black) << std::endl;
    std::cout << "Color support test completed.\n" << std::endl;
}

void test_exception_throwing() {
    std::cout << "=== Test: Exception Throwing ===" << std::endl;

    try {
        throw ecul_erro_runtime("This is a runtime error with automatic logging");
    } catch (const std::exception& e) {
        std::cout << "Caught expected runtime_error: " << e.what() << std::endl;
    }

    try {
        throw ecul_erro_what(std::invalid_argument("Invalid argument provided"));
    } catch (const std::exception& e) {
        std::cout << "Caught expected invalid_argument: " << e.what() << std::endl;
    }

    try {
        throw ecul_erro_msg("Custom context message", std::logic_error("Logic error occurred"));
    } catch (const std::exception& e) {
        std::cout << "Caught expected logic_error: " << e.what() << std::endl;
    }

    std::cout << "Exception throwing test completed.\n" << std::endl;
}

void test_critical_stop() {
    std::cout << "=== Test: Critical Stop Exception ===" << std::endl;
    std::cout << "WARNING: This will throw critical_do_not_catch_exception_stop!" << std::endl;
    std::cout << "The exception should NOT be caught by normal exception handlers." << std::endl;

    // This should terminate the program or propagate to top level
    throw ecul_stop("Critical security violation detected - testing stop exception");
}

void test_abort() {
    std::cout << "=== Test: Abort ===" << std::endl;
    std::cout << "WARNING: This will abort the program!" << std::endl;

    // This will terminate the program
    ecul_abort("Unrecoverable error - testing abort functionality");
}

void show_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [test_type]" << std::endl;
    std::cout << "Available tests:" << std::endl;
    std::cout << "  logging      - Test basic logging functionality" << std::endl;
    std::cout << "  project      - Test project identification" << std::endl;
    std::cout << "  color        - Test color initialization and support" << std::endl;
    std::cout << "  exception    - Test exception throwing with logging" << std::endl;
    std::cout << "  stop         - Test critical stop exception (will throw!)" << std::endl;
    std::cout << "  abort        - Test abort functionality (will abort!)" << std::endl;
    std::cout << "  all          - Run all safe tests (default)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string test_type = "all";

    if (argc > 1) {
        test_type = argv[1];
    }

    if (test_type == "help" || test_type == "-h" || test_type == "--help") {
        show_usage(argv[0]);
        return 0;
    }

    std::cout << "=== ECUL Library Test Suite ===" << std::endl;
    std::cout << "Running test: " << test_type << std::endl;
    std::cout << std::endl;

    // CRITICAL: Initialize colors from main() - this is required for color support
    // Colors will NOT work if this is not called!
    ecul::init_colors();

    try {
        if (test_type == "logging") {
            test_basic_logging();
        } else if (test_type == "project") {
            test_project_identification();
        } else if (test_type == "color") {
            test_color_initialization();
            test_color_support();
        } else if (test_type == "exception") {
            test_exception_throwing();
        } else if (test_type == "stop") {
            test_critical_stop();
        } else if (test_type == "abort") {
            test_abort();
        } else if (test_type == "all") {
            test_color_initialization();
            test_project_identification();
            test_color_support();
            test_basic_logging();
            test_exception_throwing();
            std::cout << "=== Safe Tests Completed Successfully ===" << std::endl;
            std::cout << "To test critical functionality, run with 'stop' or 'abort' arguments." << std::endl;
        } else {
            std::cout << "Unknown test type: " << test_type << std::endl;
            show_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "Unexpected exception: " << e.what() << std::endl;
        return 3;
    }

    return 0;
}
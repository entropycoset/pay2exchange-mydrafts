#include "libecul/ecul.hpp"
#include <iostream>
#include <stdexcept>

// Demonstration of ECUL (EntropyCoset Utils Lib) usage
int main() {
    std::cout << "=== ECUL Library Test ===" << std::endl;
    
    // Test 1: Basic logging
    std::cout << "\n--- Test 1: Basic Logging ---" << std::endl;
    ecul_info("This is an informational message");
    ecul_warn("This is a warning message");
    ecul_log_erro("This is an error message (logged but no exception)");
    
    // Test 2: Exception throwing with automatic logging
    std::cout << "\n--- Test 2: Exception Throwing ---" << std::endl;
    
    try {
        throw ecul_erro_runtime("This is a runtime error with automatic logging");
    } catch (const std::exception& e) {
        std::cout << "Caught expected runtime_error: " << e.what() << std::endl;
    }
    
    // Test 3: Custom exception with logging
    std::cout << "\n--- Test 3: Custom Exception ---" << std::endl;
    
    try {
        throw ecul_erro_what(std::invalid_argument("Invalid argument provided"));
    } catch (const std::exception& e) {
        std::cout << "Caught expected invalid_argument: " << e.what() << std::endl;
    }
    
    // Test 4: Custom exception with additional message
    std::cout << "\n--- Test 4: Custom Exception with Message ---" << std::endl;
    
    try {
        throw ecul_erro_msg("Custom context message", std::logic_error("Logic error occurred"));
    } catch (const std::exception& e) {
        std::cout << "Caught expected logic_error: " << e.what() << std::endl;
    }
    
    // Test 5: Demonstrate critical stop exception (commented out to prevent program termination)
    std::cout << "\n--- Test 5: Critical Stop Exception (commented out) ---" << std::endl;
    std::cout << "// Uncomment the following line to test critical stop exception:" << std::endl;
    std::cout << "// throw ecul_stop(\"Critical security violation detected\");" << std::endl;
    
    /*
    // UNCOMMENT TO TEST CRITICAL STOP EXCEPTION:
    try {
        throw ecul_stop("Critical security violation detected");
    // UNSAFE_LINTER_IGNORE_CATCH_ALL
    // TODO check is this OK to catch-all stop. XXX security
    } catch (...) {
        // This should NEVER happen according to our security model!
        std::cout << "ERROR: critical_do_not_catch_exception_stop was caught!" << std::endl;
    }
    */
    
    // Test 6: Demonstrate abort (commented out to prevent program termination)
    std::cout << "\n--- Test 6: Abort (commented out) ---" << std::endl;
    std::cout << "// Uncomment the following line to test abort:" << std::endl;
    std::cout << "// ecul_abort(\"Unrecoverable error - aborting program\");" << std::endl;
    
    /*
    // UNCOMMENT TO TEST ABORT:
    ecul_abort("Unrecoverable error - aborting program");
    */
    
    std::cout << "\n=== ECUL Test Completed Successfully ===" << std::endl;
    return 0;
}
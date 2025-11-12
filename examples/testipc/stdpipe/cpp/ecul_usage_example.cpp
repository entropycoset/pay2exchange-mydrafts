#include "libecul/ecul.hpp"
#include <iostream>
#include <stdexcept>

// Example showing proper ECUL usage patterns
int main() {
    std::cout << "=== ECUL Usage Examples ===" << std::endl;
    
    // Example 1: Basic logging without exceptions
    ecul_info("Application started successfully");
    ecul_warn("Configuration file not found, using defaults");
    
    // Example 2: Throwing runtime errors with automatic logging
    try {
        if (true) { // simulate error condition
            throw ecul_erro_runtime("Database connection failed");
        }
    } catch (const std::exception& e) {
        std::cout << "Handled: " << e.what() << std::endl;
    }
    
    // Example 3: Custom exceptions with context
    try {
        throw ecul_erro_msg("User validation failed", 
                           std::invalid_argument("Username cannot be empty"));
    } catch (const std::exception& e) {
        std::cout << "Handled: " << e.what() << std::endl;
    }
    
    // Example 4: Critical security violation (commented out)
    std::cout << "\nCritical security example (commented):" << std::endl;
    std::cout << "// throw ecul_stop(\"Security breach detected - unauthorized access\");" << std::endl;
    
    // Example 5: Using macros for different severity levels
    ecul_info("Processing user request");
    ecul_warn("Rate limiting threshold approached");
    
    // Example 6: Demonstrating [[nodiscard]] protection
    std::cout << "\n[[nodiscard]] protection ensures throw usage:" << std::endl;
    std::cout << "// auto ex = ecul_stop(\"msg\");  // Warning: return value not used" << std::endl;
    std::cout << "// throw ecul_stop(\"msg\");      // Correct usage" << std::endl;
    
    ecul_info("Example completed successfully");
    return 0;
}
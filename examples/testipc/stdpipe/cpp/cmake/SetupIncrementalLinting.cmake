# SetupIncrementalLinting.cmake - Dynamic setup of incremental linting
# This script is called after compile_commands.json is available to create .lint targets

# This script is run via cmake -P, so we need to include the main module
include(${CMAKE_SOURCE_DIR}/cmake/IncrementalLinting.cmake)

message(STATUS "🔧 Dynamically setting up incremental linting...")

# Check if compilation database exists
if(NOT EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    message(FATAL_ERROR "❌ compile_commands.json not found at ${CMAKE_BINARY_DIR}/compile_commands.json")
endif()

# Enable incremental linting with current configuration
enable_incremental_linting()

message(STATUS "✅ Incremental linting setup complete!")
message(STATUS "")
message(STATUS "💡 USAGE EXAMPLES:")
message(STATUS "   Lint all (incremental):     ninja lint_incremental")
message(STATUS "   Lint specific file:         ninja CMakeFiles/stdpipe_back.dir/stdpipe_back.cpp.clang-tidy.lint")
message(STATUS "   View lint results:          cat CMakeFiles/target.dir/file.cpp.clang-tidy.lint")
message(STATUS "   Generate summary:           ninja lint_summary")
message(STATUS "")
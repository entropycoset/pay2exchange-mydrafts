# GenerateLintSummary.cmake - Simple lint results summary

message(STATUS "")
message(STATUS "========================================")
message(STATUS "📊 LINT SUMMARY REPORT")  
message(STATUS "========================================")

# Find all .lint files in build directory
file(GLOB_RECURSE all_lint_files "**/*.lint")
# Remove duplicates and sort
list(REMOVE_DUPLICATES all_lint_files)
list(SORT all_lint_files)

if(NOT all_lint_files)
    message(STATUS "⚠️  No .lint files found")
    message(STATUS "   Create .lint files first with incremental linting")
    message(STATUS "========================================")
    return()
endif()

# Count files
list(LENGTH all_lint_files total_files)
message(STATUS "Found ${total_files} .lint files")

# Show actual .lint file paths for easy copy-pasting
list(SORT all_lint_files)

foreach(lint_file ${all_lint_files})
    # Make path relative to current directory for copy-pasting
    file(RELATIVE_PATH rel_lint_file "${CMAKE_CURRENT_BINARY_DIR}" "${lint_file}")
    get_filename_component(lint_name "${lint_file}" NAME)
    
    # Extract tool type
    if(lint_name MATCHES "\\.clang-tidy\\.lint$")
        set(tool "clang-tidy")
    elseif(lint_name MATCHES "\\.cppcheck\\.lint$")
        set(tool "cppcheck")
    else()
        set(tool "unknown")
    endif()
    
    message(STATUS "  📄 ${rel_lint_file} (${tool})")
endforeach()

message(STATUS "")
message(STATUS "💡 View lint results:")
message(STATUS "   cat lint_output/filename.clang-tidy.lint")
message(STATUS "   cat lint_output/filename.cppcheck.lint")

message(STATUS "")
message(STATUS "💡 USAGE:")
message(STATUS "   View file: cat build_test/path/to/file.cpp.lint") 
message(STATUS "   Individual lint: ninja CMakeFiles/target.dir/file.cpp.clang-tidy.lint")
message(STATUS "========================================")
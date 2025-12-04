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

# Filter files: real warnings vs clean files
set(files_with_real_warnings "")
set(files_clean_but_linted "")

foreach(lint_file ${all_lint_files})
    file(SIZE "${lint_file}" file_size)
    if(file_size GREATER 0)
        # Read file content to check if it contains actual warnings
        file(READ "${lint_file}" file_content)
        # Check if file contains actual warnings (not just "Suppressed X warnings")
        string(REGEX MATCH "warning:|error:|note:" has_actual_warnings "${file_content}")
        if(has_actual_warnings)
            list(APPEND files_with_real_warnings "${lint_file}")
        else()
            # File was linted but has no actionable warnings (only suppressed)
            list(APPEND files_clean_but_linted "${lint_file}")
        endif()
    endif()
endforeach()

# Count files
list(LENGTH all_lint_files total_files)
list(LENGTH files_with_real_warnings files_with_warnings)
list(LENGTH files_clean_but_linted files_clean)

message(STATUS "Found ${total_files} .lint files (${files_with_warnings} with actionable warnings, ${files_clean} clean)")

if(NOT files_with_real_warnings)

message(STATUS "🎉 All ${files_clean} linted files passed with no actionable warnings!")
    message(STATUS "   (Files may have suppressed warnings from system headers)")
    message(STATUS "========================================")
    return()
endif()

# Show files with actual actionable warnings
message(STATUS "")
message(STATUS "⚠️  FILES WITH ACTIONABLE WARNINGS:")

foreach(lint_file ${files_with_real_warnings})
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
    
    # Count actual warnings in the file
    file(READ "${lint_file}" file_content)
    string(REGEX MATCHALL "warning:" warning_matches "${file_content}")
    list(LENGTH warning_matches warning_count)
    
    message(STATUS "  📄 ${rel_lint_file} (${tool}) - ${warning_count} warnings")
endforeach()

message(STATUS "")
message(STATUS "✅ ${files_clean} files linted with no actionable warnings (clean)")

message(STATUS "")
message(STATUS "💡 View lint results:")
message(STATUS "   cat lint_output/filename.clang-tidy.lint")
message(STATUS "   cat lint_output/filename.cppcheck.lint")

message(STATUS "")
message(STATUS "💡 USAGE:")
message(STATUS "   View file: cat build_test/path/to/file.cpp.lint") 
message(STATUS "   Individual lint: ninja CMakeFiles/target.dir/file.cpp.clang-tidy.lint")
message(STATUS "========================================") 
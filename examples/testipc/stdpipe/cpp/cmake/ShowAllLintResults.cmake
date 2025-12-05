# ShowAllLintResults.cmake - Display all lint file contents with headers

message(STATUS "")
message(STATUS "========================================")
message(STATUS "📋 ALL LINT RESULTS")  
message(STATUS "========================================")

# Find all .lint files in build directory
file(GLOB_RECURSE all_lint_files "**/*.lint")
# Remove duplicates and sort
list(REMOVE_DUPLICATES all_lint_files)
list(SORT all_lint_files)

if(NOT all_lint_files)
    message(STATUS "⚠️  No .lint files found")
    message(STATUS "   Run 'make lint' first to generate lint files")
    message(STATUS "========================================")
    return()
endif()

# Filter out empty files
set(non_empty_lint_files "")
foreach(lint_file ${all_lint_files})
    file(SIZE "${lint_file}" file_size)
    if(file_size GREATER 0)
        list(APPEND non_empty_lint_files "${lint_file}")
    endif()
endforeach()

list(LENGTH all_lint_files total_files)
list(LENGTH non_empty_lint_files files_with_content)

if(NOT non_empty_lint_files)
    message(STATUS "🎉 All ${total_files} lint files are empty - no warnings found!")
    message(STATUS "========================================")
    return()
endif()

message(STATUS "Showing ${files_with_content} of ${total_files} lint files with content:")
message(STATUS "")

# Show each non-empty lint file with header and content
foreach(lint_file ${non_empty_lint_files})
    # Make path relative to current directory for easy copy-pasting
    file(RELATIVE_PATH rel_lint_file "${CMAKE_CURRENT_BINARY_DIR}" "${lint_file}")
    
    # Extract tool type for display
    get_filename_component(lint_name "${lint_file}" NAME)
    if(lint_name MATCHES "\\.clang-tidy\\.lint$")
        set(tool_type "clang-tidy")
    elseif(lint_name MATCHES "\\.cppcheck\\.lint$")
        set(tool_type "cppcheck")
    else()
        set(tool_type "lint")
    endif()
    
    # Show file header
    message(STATUS "")
    message(STATUS "==== 📄 ${rel_lint_file} (${tool_type}) ====")
    
    # Read and display file content
    file(READ "${lint_file}" file_content)
    # Remove trailing newlines for cleaner output
    string(REGEX REPLACE "\n+$" "" file_content "${file_content}")
    
    # Split content into lines and print each line
    string(REPLACE "\n" ";" content_lines "${file_content}")
    foreach(line ${content_lines})
        message(STATUS "${line}")
    endforeach()
    
    message(STATUS "")
endforeach()

message(STATUS "========================================")
message(STATUS "📋 END OF LINT RESULTS")
message(STATUS "")
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

# Categorize and display files
foreach(lint_file ${all_lint_files})
    get_filename_component(lint_name "${lint_file}" NAME)
    get_filename_component(lint_dir "${lint_file}" DIRECTORY)
    
    # Extract source file name from .lint filename
    string(REGEX REPLACE "\\.clang-tidy\\.lint$|\\.cppcheck\\.lint$" "" source_name "${lint_name}")
    
    # Determine if it's a library or main target
    if(lint_dir MATCHES "lib[^/]+/CMakeFiles")
        string(REGEX REPLACE ".*/lib([^/]+)/CMakeFiles.*" "\\1" lib_name "${lint_dir}")
        message(STATUS "  📄 lib${lib_name}: ${source_name}")
    else()
        message(STATUS "  📄 ${source_name}")
    endif()
endforeach()

message(STATUS "")
message(STATUS "💡 USAGE:")
message(STATUS "   View file: cat build_test/path/to/file.cpp.lint") 
message(STATUS "   Individual lint: ninja CMakeFiles/target.dir/file.cpp.clang-tidy.lint")
message(STATUS "========================================")
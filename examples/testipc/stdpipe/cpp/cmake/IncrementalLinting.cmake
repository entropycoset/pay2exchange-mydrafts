# IncrementalLinting.cmake - Per-file incremental static analysis
# Creates .lint files for each .cpp file that rebuild based on dependency changes
#
# Benefits:
# - Only lint files that changed or have changed dependencies
# - Persistent lint results (cat file.cpp.lint anytime)
# - Parallel execution of individual lint targets
# - 10x-100x faster for typical development workflows

# Parse compile_commands.json to extract compilation units and their dependencies
function(parse_compile_commands_for_linting)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
        message(STATUS "⚠️  compile_commands.json not found - incremental linting requires compilation database")
        return()
    endif()
    
    # Read compilation database
    file(READ "${CMAKE_BINARY_DIR}/compile_commands.json" compile_db_content)
    
    # Extract source files and their compilation info
    # Note: This is a simplified JSON parser - for production, consider using a proper JSON parser
    string(REGEX MATCHALL "\"file\": \"([^\"]+)\"" file_matches "${compile_db_content}")
    string(REGEX MATCHALL "\"output\": \"([^\"]+)\"" output_matches "${compile_db_content}")
    
    set(ALL_LINT_TARGETS "" PARENT_SCOPE)
    set(ALL_CLANG_TIDY_LINT_FILES "" PARENT_SCOPE)
    set(ALL_CPPCHECK_LINT_FILES "" PARENT_SCOPE)
    
    list(LENGTH file_matches num_files)
    math(EXPR max_index "${num_files} - 1")
    
    foreach(i RANGE 0 ${max_index})
        list(GET file_matches ${i} file_entry)
        list(GET output_matches ${i} output_entry)
        
        # Extract file path from "file": "/path/to/file.cpp"
        string(REGEX REPLACE "\"file\": \"([^\"]+)\"" "\\1" source_file "${file_entry}")
        # Extract output path from "output": "CMakeFiles/target.dir/file.cpp.o"  
        string(REGEX REPLACE "\"output\": \"([^\"]+)\"" "\\1" object_file "${output_entry}")
        
        # Skip if not a .cpp file (we only want compilation units)
        if(NOT source_file MATCHES "\\.cpp$")
            continue()
        endif()
        
        # Convert absolute path to relative path from source dir
        file(RELATIVE_PATH rel_source_file "${CMAKE_SOURCE_DIR}" "${source_file}")
        
        # Skip files outside our source tree
        if(rel_source_file MATCHES "^\\.\\./")
            continue()
        endif()
        
        message(STATUS "📝 Setting up incremental lint for: ${rel_source_file}")
        
        # Create lint targets for this source file
        create_lint_targets_for_file("${source_file}" "${object_file}" "${rel_source_file}")
    endforeach()
endfunction()

# Create individual lint targets for a single source file
function(create_lint_targets_for_file source_file object_file rel_source_file)
    # Derive base path for lint files from object file path
    string(REGEX REPLACE "\\.o$" "" base_output_path "${object_file}")
    
    # Create clang-tidy lint target
    if(CLANG_TIDY_AVAILABLE)
        create_clang_tidy_target("${source_file}" "${base_output_path}" "${rel_source_file}")
    endif()
    
    # Create cppcheck lint target (only for .cpp files)
    if(CPPCHECK_AVAILABLE AND rel_source_file MATCHES "\\.cpp$")
        create_cppcheck_target("${source_file}" "${base_output_path}" "${rel_source_file}")
    endif()
endfunction()

# Create clang-tidy lint target for a single file
function(create_clang_tidy_target source_file base_output_path rel_source_file)
    set(lint_file "${CMAKE_BINARY_DIR}/${base_output_path}.clang-tidy.lint")
    
    add_custom_command(
        OUTPUT "${lint_file}"
        COMMAND ${CMAKE_COMMAND} -E echo "🔍 Running clang-tidy on ${rel_source_file}..."
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            --header-filter=${CMAKE_SOURCE_DIR}/.*
            --quiet
            --format-style=file
            -p ${CMAKE_BINARY_DIR}
            "${source_file}"
            > "${lint_file}" 2>&1 || echo "LINT_COMPLETED_WITH_WARNINGS" >> "${lint_file}"
        COMMAND ${CMAKE_COMMAND} -E echo "✅ clang-tidy completed for ${rel_source_file}"
        DEPENDS 
            "${source_file}"
            "${CMAKE_SOURCE_DIR}/.clang-tidy"
            "${CMAKE_BINARY_DIR}/compile_commands.json"
        IMPLICIT_DEPENDS CXX "${source_file}"
        COMMENT "🔍 Linting ${rel_source_file} with clang-tidy"
        VERBATIM
    )
    
    # Add to global list for aggregation targets
    set(ALL_CLANG_TIDY_LINT_FILES ${ALL_CLANG_TIDY_LINT_FILES} "${lint_file}" PARENT_SCOPE)
endfunction()

# Create cppcheck lint target for a single file
function(create_cppcheck_target source_file base_output_path rel_source_file)
    set(lint_file "${CMAKE_BINARY_DIR}/${base_output_path}.cppcheck.lint")
    
    add_custom_command(
        OUTPUT "${lint_file}"
        COMMAND ${CMAKE_COMMAND} -E echo "🔍 Running cppcheck on ${rel_source_file}..."
        COMMAND ${CPPCHECK_EXECUTABLE}
            --enable=warning,performance,portability
            --std=c++17
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --suppress=unmatchedSuppression
            --suppress=unusedStructMember
            --suppress=shadowVariable
            --suppress=useStlAlgorithm
            --suppress=constParameterPointer
            --suppress=constVariablePointer
            -I${CMAKE_SOURCE_DIR}
            -I${CMAKE_SOURCE_DIR}/libvalidcolor
            -I${CMAKE_SOURCE_DIR}/libcmdformat
            -I${CMAKE_SOURCE_DIR}/libstdpipeutil
            -I${CMAKE_SOURCE_DIR}/libecul
            "${source_file}"
            > "${lint_file}" 2>&1 || echo "LINT_COMPLETED_WITH_WARNINGS" >> "${lint_file}"
        COMMAND ${CMAKE_COMMAND} -E echo "✅ cppcheck completed for ${rel_source_file}"
        DEPENDS "${source_file}"
        IMPLICIT_DEPENDS CXX "${source_file}"
        COMMENT "🔍 Linting ${rel_source_file} with cppcheck"
        VERBATIM
    )
    
    # Add to global list for aggregation targets
    set(ALL_CPPCHECK_LINT_FILES ${ALL_CPPCHECK_LINT_FILES} "${lint_file}" PARENT_SCOPE)
endfunction()

# Create aggregation targets for incremental linting
function(create_incremental_lint_targets)
    message(STATUS "🚀 Setting up incremental linting system...")
    
    # Ensure we have a compilation database
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
        message(STATUS "⚠️  No compile_commands.json found - incremental linting will be created after first build")
        return()
    endif()
    
    # Parse compilation database and create individual lint targets
    parse_compile_commands_for_linting()
    
    # Create incremental clang-tidy aggregation target
    if(CLANG_TIDY_AVAILABLE AND ALL_CLANG_TIDY_LINT_FILES)
        add_custom_target(lint_clang_tidy_incremental
            DEPENDS ${ALL_CLANG_TIDY_LINT_FILES}
            COMMENT "🔍 Running incremental clang-tidy on changed files"
        )
        
        message(STATUS "✅ Created incremental clang-tidy target with ${list(LENGTH ALL_CLANG_TIDY_LINT_FILES)} source files")
    endif()
    
    # Create incremental cppcheck aggregation target  
    if(CPPCHECK_AVAILABLE AND ALL_CPPCHECK_LINT_FILES)
        add_custom_target(lint_cppcheck_incremental
            DEPENDS ${ALL_CPPCHECK_LINT_FILES}
            COMMENT "🔍 Running incremental cppcheck on changed files"
        )
        
        message(STATUS "✅ Created incremental cppcheck target with ${list(LENGTH ALL_CPPCHECK_LINT_FILES)} source files")
    endif()
    
    # Create combined incremental lint target
    set(INCREMENTAL_LINT_DEPS "")
    if(CLANG_TIDY_AVAILABLE AND ALL_CLANG_TIDY_LINT_FILES)
        list(APPEND INCREMENTAL_LINT_DEPS lint_clang_tidy_incremental)
    endif()
    if(CPPCHECK_AVAILABLE AND ALL_CPPCHECK_LINT_FILES)
        list(APPEND INCREMENTAL_LINT_DEPS lint_cppcheck_incremental)
    endif()
    
    if(INCREMENTAL_LINT_DEPS)
        add_custom_target(lint_incremental
            DEPENDS ${INCREMENTAL_LINT_DEPS}
            COMMENT "🚀 Running all incremental linters"
        )
        
        # If the main lint target exists, add incremental linting as a dependency
        if(TARGET lint)
            add_dependencies(lint lint_incremental)
            message(STATUS "✅ Added incremental linting to main lint target")
        endif()
        
        message(STATUS "✅ Created combined incremental lint target")
    endif()
endfunction()

# Main function to enable incremental linting
function(enable_incremental_linting)
    create_incremental_lint_targets()
endfunction()
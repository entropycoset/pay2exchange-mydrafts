cmake_minimum_required(VERSION 3.10)
project(stdpipe)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only set CMP0167 if the CMake version knows about it
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW) # I know FindBoost is gone, I’m fine with using Boost’s own config
endif()

# =====================================================
# Linter Detection and Configuration
# =====================================================

# Variables to track linter availability and warnings
set(LINTER_WARNINGS "")
set(CLANG_TIDY_AVAILABLE FALSE)
set(CPPCHECK_AVAILABLE FALSE)

# Check for clang-tidy
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    set(CLANG_TIDY_AVAILABLE TRUE)
    message(STATUS "✅ Found clang-tidy: ${CLANG_TIDY_EXECUTABLE}")
else()
    set(CLANG_TIDY_AVAILABLE FALSE)
    set(LINTER_WARNING_CLANG_TIDY "❌ WARNING: clang-tidy not found! Install with: apt install clang-tidy")
    set(LINTER_WARNINGS "${LINTER_WARNINGS}\n${LINTER_WARNING_CLANG_TIDY}")
    message(WARNING "${LINTER_WARNING_CLANG_TIDY}")
endif()

# Check for cppcheck
find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
if(CPPCHECK_EXECUTABLE)
    set(CPPCHECK_AVAILABLE TRUE)
    message(STATUS "✅ Found cppcheck: ${CPPCHECK_EXECUTABLE}")
else()
    set(CPPCHECK_AVAILABLE FALSE)
    set(LINTER_WARNING_CPPCHECK "❌ WARNING: cppcheck not found! Install with: apt install cppcheck")
    set(LINTER_WARNINGS "${LINTER_WARNINGS}\n${LINTER_WARNING_CPPCHECK}")
    message(WARNING "${LINTER_WARNING_CPPCHECK}")
endif()

# Collect all source files for linting
file(GLOB_RECURSE ALL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/*.hpp"
    "${CMAKE_SOURCE_DIR}/*.h"
)
# Print the resulting list
message(STATUS "Collected source files for linting (before excluding/editing this list):")
foreach(src ${ALL_SOURCE_FILES})
    message(STATUS "  ${src}")
endforeach()

# Filter out build directory files and CMake generated files

# list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "${CMAKE_BINARY_DIR}/.*")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/CMakeFiles/.*")

# For cppcheck, only use .cpp files to avoid issues with incomplete headers
file(GLOB_RECURSE CPP_SOURCE_FILES "${CMAKE_SOURCE_DIR}/*.cpp")
#list(FILTER CPP_SOURCE_FILES EXCLUDE REGEX "${CMAKE_BINARY_DIR}/.*")
list(FILTER CPP_SOURCE_FILES EXCLUDE REGEX ".*/CMakeFiles/.*")

# Create lint targets
if(CLANG_TIDY_AVAILABLE OR CPPCHECK_AVAILABLE)
    # Custom security linting script target
    add_custom_target(lint_security
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/lint_security_violations.sh
        COMMAND ${CMAKE_SOURCE_DIR}/lint_security_violations.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "🔒 Running security violation checks"
    )

    # Make the lint script executable during configure
    if(UNIX)
        execute_process(COMMAND chmod +x ${CMAKE_SOURCE_DIR}/lint_security_violations.sh)
    endif()
endif()

# clang-tidy target
if(CLANG_TIDY_AVAILABLE)
	message(STATUS "Collected source files for linting (right before use):")
	foreach(src ${ALL_SOURCE_FILES})
		message(STATUS "  ${src}")
	endforeach()

    add_custom_target(lint_clang_tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
            --header-filter=.*
            ${ALL_SOURCE_FILES}
            --
            -std=c++17
            -I${CMAKE_SOURCE_DIR}
            -I${CMAKE_SOURCE_DIR}/libvalidcolor
            -I${CMAKE_SOURCE_DIR}/libcmdformat
            -I${CMAKE_SOURCE_DIR}/libstdpipeutil
            -I${Boost_INCLUDE_DIRS} || true
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "🔍 Running clang-tidy on all source files (informational)"
    )
endif()

# cppcheck target
if(CPPCHECK_AVAILABLE)
    add_custom_target(lint_cppcheck
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
            -I${Boost_INCLUDE_DIRS}
            ${CPP_SOURCE_FILES} || true
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "🔍 Running cppcheck on .cpp files (informational - warnings only)"
    )
endif()

# Combined lint target
set(LINT_DEPENDENCIES "")
if(CLANG_TIDY_AVAILABLE OR CPPCHECK_AVAILABLE)
    list(APPEND LINT_DEPENDENCIES lint_security)
endif()
if(CLANG_TIDY_AVAILABLE)
    list(APPEND LINT_DEPENDENCIES lint_clang_tidy)
endif()
if(CPPCHECK_AVAILABLE)
    list(APPEND LINT_DEPENDENCIES lint_cppcheck)
endif()

if(LINT_DEPENDENCIES)
    add_custom_target(lint
        DEPENDS ${LINT_DEPENDENCIES}
        COMMENT "🔍 Running all available linters"
    )

    # Make lint target PHONY (always run)
    set_target_properties(lint PROPERTIES
        EXCLUDE_FROM_ALL FALSE
    )

    if(CLANG_TIDY_AVAILABLE)
        set_target_properties(lint_clang_tidy PROPERTIES EXCLUDE_FROM_ALL FALSE)
    endif()
    if(CPPCHECK_AVAILABLE)
        set_target_properties(lint_cppcheck PROPERTIES EXCLUDE_FROM_ALL FALSE)
    endif()
    set_target_properties(lint_security PROPERTIES EXCLUDE_FROM_ALL FALSE)

    message(STATUS "✅ Lint target created with: ${LINT_DEPENDENCIES}")
else()
    # Create a dummy lint target that shows warnings
    add_custom_target(lint
        COMMAND ${CMAKE_COMMAND} -E echo "❌ No linters available - install clang-tidy and/or cppcheck"
        COMMENT "⚠️  Linters not available"
    )
endif()

# =====================================================
# Prepare install-build dir section
# =====================================================

# Check for P2E_TOOLS_HOME environment variable
set(P2E_TOOLS_HOME $ENV{P2E_TOOLS_HOME})
set(P2E_ENV_WARNING FALSE)

if(NOT P2E_TOOLS_HOME)
		# Default to ~/p2e-tools if not set
		set(P2E_TOOLS_HOME "$ENV{HOME}/p2e-tools")
		set(P2E_ENV_WARNING TRUE)
		message(WARNING "❌ P2E_TOOLS_HOME environment variable not set. Defaulting to: ${P2E_TOOLS_HOME}")
endif()

# Set the tools installation directory
set(P2E_TOOLS_DIR "${P2E_TOOLS_HOME}/tools")

# Create the tools directory if it doesn't exist
file(MAKE_DIRECTORY "${P2E_TOOLS_DIR}")

if(EXISTS "${P2E_TOOLS_DIR}")
		message(STATUS "✅ Tools directory confirmed: ${P2E_TOOLS_DIR}")
else()
		message(FATAL_ERROR "❌ Failed to create tools directory: ${P2E_TOOLS_DIR}")
endif()

# Find Boost
find_package(Boost REQUIRED COMPONENTS system filesystem iostreams program_options)

# Find nlohmann/json
find_package(nlohmann_json REQUIRED)

# Add subdirectories for libraries
add_subdirectory(libvalidcolor)
add_subdirectory(libcmdformat)
add_subdirectory(libstdpipeutil)
add_subdirectory(libecul)

# Create executables
add_executable(stdpipe_back stdpipe_back.cpp)
add_executable(stdpipe_serv stdpipe_serv.cpp)
add_executable(clean_exec clean_exec.cpp envcleaner.cpp)
add_executable(print_env_extra print_env_extra.cpp)
add_executable(ecul_test ecul_test.cpp)

# Link Boost and libraries to stdpipe_back and stdpipe_serv
target_link_libraries(stdpipe_back ${Boost_LIBRARIES} ncurses libvalidcolor libcmdformat libstdpipeutil libecul nlohmann_json::nlohmann_json)
target_include_directories(stdpipe_back PRIVATE ${Boost_INCLUDE_DIRS} libvalidcolor libcmdformat libstdpipeutil libecul)

target_link_libraries(stdpipe_serv ${Boost_LIBRARIES} libcmdformat libstdpipeutil libecul)
target_include_directories(stdpipe_serv PRIVATE ${Boost_INCLUDE_DIRS} libcmdformat libstdpipeutil libecul)

# Link Boost and ECUL to clean_exec
target_link_libraries(clean_exec ${Boost_LIBRARIES} libecul)
target_include_directories(clean_exec PRIVATE ${Boost_INCLUDE_DIRS} libecul)

# Link ECUL library to test
target_link_libraries(ecul_test libecul)
target_include_directories(ecul_test PRIVATE libecul)

# Copy simple_test.sh to build directory
configure_file(${CMAKE_SOURCE_DIR}/simple_test.sh ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh COPYONLY)

# Make the copied script executable
if(UNIX)
		add_custom_command(TARGET stdpipe_serv POST_BUILD
				COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh
				COMMENT "Making simple_test.sh executable"
		)
endif()

# Create a custom target for the test script to ensure proper dependency tracking
add_custom_target(copy_test_script
		COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh
		COMMENT "Ensuring test script is copied and executable"
)

# Add explicit "build_all" target that includes binaries, test script, linting, and installation
if(LINT_DEPENDENCIES)
	add_custom_target(build_all
		DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra ecul_test copy_test_script lint install_tools
		COMMENT "Building all executables, copying test script, running linters, and installing tools"
	)
else()
	add_custom_target(build_all
		DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra ecul_test copy_test_script install_tools
		COMMENT "Building all executables, copying test script, and installing tools (linters not available)"
	)
endif()

# Add custom target "run" that executes the test script
add_custom_target(run
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh
		DEPENDS build_all clean_exec stdpipe_back stdpipe_serv
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Running test script (requires clean_exec, stdpipe_back, stdpipe_serv)"
)

# Copy test automation script to build directory
configure_file(${CMAKE_SOURCE_DIR}/test_env_fd_cleanup.py ${CMAKE_CURRENT_BINARY_DIR}/test_env_fd_cleanup.py COPYONLY)

# Make the test script executable
if(UNIX)
		add_custom_command(TARGET print_env_extra POST_BUILD
				COMMAND chmod +x ${CMAKE_CURRENT_BINARY_DIR}/test_env_fd_cleanup.py
				COMMENT "Making test_env_fd_cleanup.py executable"
		)
endif()

# Add custom target "test2" that executes the environment and FD cleanup tests
add_custom_target(test2
		COMMAND python3 ${CMAKE_CURRENT_BINARY_DIR}/test_env_fd_cleanup.py
		DEPENDS build_all clean_exec stdpipe_back print_env_extra
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Running environment and FD cleanup tests (requires clean_exec, stdpipe_back, print_env_extra)"
)

# =====================================================
# Post-build installation to tools directory
# =====================================================

# Set the fallback HOME tools directory
set(HOME_TOOLS_DIR "$ENV{HOME}/p2e-tools/tools")

# Create a PHONY target that always reinstalls all executables
add_custom_target(install_tools
	COMMAND ${CMAKE_COMMAND} -E make_directory "${P2E_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${HOME_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_back> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_back> "${HOME_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_serv> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_serv> "${HOME_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:clean_exec> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:clean_exec> "${HOME_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:print_env_extra> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:print_env_extra> "${HOME_TOOLS_DIR}/"
	DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra
	COMMENT "✅ Installing all executables to ${P2E_TOOLS_DIR} and ${HOME_TOOLS_DIR} (always runs)"
)

# Make install_tools a PHONY target (always runs)
set_target_properties(install_tools PROPERTIES
	EXCLUDE_FROM_ALL FALSE
)

# Also keep POST_BUILD installation for immediate installation after individual builds
add_custom_command(TARGET stdpipe_back POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${P2E_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${HOME_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_back> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_back> "${HOME_TOOLS_DIR}/"
	COMMENT "✅ Installing stdpipe_back to ${P2E_TOOLS_DIR} and ${HOME_TOOLS_DIR}"
)

add_custom_command(TARGET stdpipe_serv POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${P2E_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${HOME_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_serv> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_serv> "${HOME_TOOLS_DIR}/"
	COMMENT "✅ Installing stdpipe_serv to ${P2E_TOOLS_DIR} and ${HOME_TOOLS_DIR}"
)

add_custom_command(TARGET clean_exec POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${P2E_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${HOME_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:clean_exec> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:clean_exec> "${HOME_TOOLS_DIR}/"
	COMMENT "✅ Installing clean_exec to ${P2E_TOOLS_DIR} and ${HOME_TOOLS_DIR}"
)

add_custom_command(TARGET print_env_extra POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${P2E_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${HOME_TOOLS_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:print_env_extra> "${P2E_TOOLS_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:print_env_extra> "${HOME_TOOLS_DIR}/"
	COMMENT "✅ Installing print_env_extra to ${P2E_TOOLS_DIR} and ${HOME_TOOLS_DIR}"
)

# =====================================================
# After-build target for final warnings
# =====================================================

# Create after_build virtual target that runs after all builds (including lint and installation)
if(LINT_DEPENDENCIES)
	add_custom_target(after_build ALL
		DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra ecul_test lint install_tools
		COMMENT "✅ All executables built, installed, and linted"
	)
else()
	add_custom_target(after_build ALL
		DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra ecul_test install_tools
		COMMENT "✅ All executables built and installed (linters not available)"
	)
endif()

# Show final warnings for P2E_TOOLS_HOME and linters
add_custom_command(TARGET after_build POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
	COMMENT "Final status and warnings"
)

# P2E_TOOLS_HOME warning
if(P2E_ENV_WARNING)
	add_custom_command(TARGET after_build POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "❌ WARNING: P2E_TOOLS_HOME not set in environment"
		COMMAND ${CMAKE_COMMAND} -E echo "Using default: ${P2E_TOOLS_HOME}"
		COMMAND ${CMAKE_COMMAND} -E echo "Consider setting P2E_TOOLS_HOME environment variable"
		COMMAND ${CMAKE_COMMAND} -E echo ""
		COMMENT "P2E_TOOLS_HOME warning"
	)
endif()

# Linter warnings
if(NOT CLANG_TIDY_AVAILABLE)
	add_custom_command(TARGET after_build POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "❌ WARNING: clang-tidy not found!"
		COMMAND ${CMAKE_COMMAND} -E echo "Install with: apt install clang-tidy"
		COMMAND ${CMAKE_COMMAND} -E echo ""
		COMMENT "clang-tidy warning"
	)
endif()

if(NOT CPPCHECK_AVAILABLE)
	add_custom_command(TARGET after_build POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "❌ WARNING: cppcheck not found!"
		COMMAND ${CMAKE_COMMAND} -E echo "Install with: apt install cppcheck"
		COMMAND ${CMAKE_COMMAND} -E echo ""
		COMMENT "cppcheck warning"
	)
endif()

# Success message
if(LINT_DEPENDENCIES)
	add_custom_command(TARGET after_build POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "✅ Build complete - executables installed and linting passed"
		COMMAND ${CMAKE_COMMAND} -E echo "   Installed to: ${P2E_TOOLS_DIR}"
		COMMAND ${CMAKE_COMMAND} -E echo "   Available linters: ${LINT_DEPENDENCIES}"
		COMMENT "Build success with linting"
	)
else()
	add_custom_command(TARGET after_build POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "⚠️  Build complete - executables installed (linters missing)"
		COMMAND ${CMAKE_COMMAND} -E echo "   Installed to: ${P2E_TOOLS_DIR}"
		COMMAND ${CMAKE_COMMAND} -E echo "   Install clang-tidy and cppcheck for code quality checks"
		COMMENT "Build success without linting"
	)
endif()

add_custom_command(TARGET after_build POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMENT "Final status footer"
)

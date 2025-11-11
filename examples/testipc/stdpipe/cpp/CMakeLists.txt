cmake_minimum_required(VERSION 3.10)
project(stdpipe)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Create executables
add_executable(stdpipe_back stdpipe_back.cpp)
add_executable(stdpipe_serv stdpipe_serv.cpp)
add_executable(clean_exec clean_exec.cpp envcleaner.cpp)
add_executable(print_env_extra print_env_extra.cpp)

# Link Boost to stdpipe_back and stdpipe_serv
target_link_libraries(stdpipe_back ${Boost_LIBRARIES})
target_include_directories(stdpipe_back PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(stdpipe_serv ${Boost_LIBRARIES})
target_include_directories(stdpipe_serv PRIVATE ${Boost_INCLUDE_DIRS})

# Link Boost to clean_exec
target_link_libraries(clean_exec ${Boost_LIBRARIES})
target_include_directories(clean_exec PRIVATE ${Boost_INCLUDE_DIRS})

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

# Add explicit "build_all" target that includes binaries and test script
add_custom_target(build_all
    DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra copy_test_script
    COMMENT "Building all executables and copying test script"
)

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

# Copy executables to tools directory after building
add_custom_command(TARGET stdpipe_back POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_back> "${P2E_TOOLS_DIR}/"
    COMMENT "✅ Installing stdpipe_back to ${P2E_TOOLS_DIR}"
)

add_custom_command(TARGET stdpipe_serv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:stdpipe_serv> "${P2E_TOOLS_DIR}/"
    COMMENT "✅ Installing stdpipe_serv to ${P2E_TOOLS_DIR}"
)

add_custom_command(TARGET clean_exec POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:clean_exec> "${P2E_TOOLS_DIR}/"
    COMMENT "✅ Installing clean_exec to ${P2E_TOOLS_DIR}"
)

add_custom_command(TARGET print_env_extra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:print_env_extra> "${P2E_TOOLS_DIR}/"
    COMMENT "✅ Installing print_env_extra to ${P2E_TOOLS_DIR}"
)

# =====================================================
# After-build target for final warnings
# =====================================================

# Create after_build virtual target that runs after all builds
add_custom_target(after_build ALL
    DEPENDS stdpipe_back stdpipe_serv clean_exec print_env_extra
    COMMENT "✅ All executables built and installed"
)

# Show final warning if P2E_TOOLS_HOME was not set
if(P2E_ENV_WARNING)
    add_custom_command(TARGET after_build POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
        COMMAND ${CMAKE_COMMAND} -E echo "❌ WARNING: P2E_TOOLS_HOME not set in environment"
        COMMAND ${CMAKE_COMMAND} -E echo "Using default: ${P2E_TOOLS_HOME}"
        COMMAND ${CMAKE_COMMAND} -E echo "Consider setting P2E_TOOLS_HOME environment variable"
        COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMENT "Final warning about P2E_TOOLS_HOME"
    )
else()
    add_custom_command(TARGET after_build POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "✅ Build complete - all executables installed to ${P2E_TOOLS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMENT "Build completion confirmation"
    )
endif()
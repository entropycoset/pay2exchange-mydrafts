cmake_minimum_required(VERSION 3.10)
project(stdpipe)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Boost
find_package(Boost REQUIRED COMPONENTS system filesystem iostreams)

# Create executables
add_executable(stdpipe_back stdpipe_back.cpp)
add_executable(stdpipe_serv stdpipe_serv.cpp)
add_executable(safe_exec safe_exec.cpp)

# Link Boost to stdpipe_back and stdpipe_serv
target_link_libraries(stdpipe_back ${Boost_LIBRARIES})
target_include_directories(stdpipe_back PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(stdpipe_serv ${Boost_LIBRARIES})
target_include_directories(stdpipe_serv PRIVATE ${Boost_INCLUDE_DIRS})

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
    DEPENDS stdpipe_back stdpipe_serv safe_exec copy_test_script
    COMMENT "Building all executables and copying test script"
)

# Add custom target "run" that executes the test script
add_custom_target(run
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/simple_test.sh
    DEPENDS build_all safe_exec
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running test script"
)
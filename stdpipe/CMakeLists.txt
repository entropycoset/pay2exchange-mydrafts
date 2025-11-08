cmake_minimum_required(VERSION 3.10)
project(stdpipe)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create executables
add_executable(stdpipe_back stdpipe_back.cpp)
add_executable(stdpipe_serv stdpipe_serv.cpp)

# Add custom target "run" that pipes stdpipe_back output to stdpipe_serv
add_custom_target(run
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/stdpipe_back | ${CMAKE_CURRENT_BINARY_DIR}/stdpipe_serv
    DEPENDS stdpipe_back stdpipe_serv
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running stdpipe_back | stdpipe_serv"
)
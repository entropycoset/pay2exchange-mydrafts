cmake_minimum_required(VERSION 3.10)

# Create static library for ECUL (EntropyCoset Utils Lib)
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(libecul STATIC ${SRC_FILES})


# Set target properties
set_target_properties(libecul PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(libecul PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Compiler-specific warnings for better code quality
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(libecul PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror=return-type
        -Werror=uninitialized
    )
endif()

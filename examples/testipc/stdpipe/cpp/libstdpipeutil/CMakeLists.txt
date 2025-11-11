# libstdpipeutil - Shared utilities for stdpipe programs
add_library(libstdpipeutil STATIC libstdpipeutil.cpp)

# Set the target name to match the directory name
set_target_properties(libstdpipeutil PROPERTIES OUTPUT_NAME stdpipeutil)

# Include directories for this library
target_include_directories(libstdpipeutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Find Boost components needed
find_package(Boost REQUIRED COMPONENTS iostreams)

# Link with Boost
target_link_libraries(libstdpipeutil PUBLIC Boost::iostreams)
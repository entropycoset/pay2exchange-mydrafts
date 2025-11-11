# Create the libcmdformat library
add_library(libcmdformat libcmdformat.cpp)

# Set target properties to remove the "lib" prefix
set_target_properties(libcmdformat PROPERTIES PREFIX "")

# Set include directories for the library
target_include_directories(libcmdformat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Create the libvalidcolor library
add_library(libvalidcolor libvalidcolor.cpp)

# Set target properties to remove the "lib" prefix
set_target_properties(libvalidcolor PROPERTIES PREFIX "")

# Set include directories for the library
target_include_directories(libvalidcolor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link ncurses library
target_link_libraries(libvalidcolor ncurses)
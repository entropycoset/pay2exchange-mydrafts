cmake_minimum_required(VERSION 3.16)
project(GridTerm LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17) # 17 for std::filesystem
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)  # needed for moc includes
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# Try to find qtermwidget-qt6 via pkg-config; fall back to system paths if pkg-config not available
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(QTERMWIDGET QUIET qtermwidget6)
endif()

if(QTERMWIDGET_FOUND)
	# Use pkg-config results
	message(STATUS "Found qtermwidget-qt6 via pkg-config: ${QTERMWIDGET_INCLUDEDIR}")
	set(QTERMWIDGET_INCLUDE_DIRS ${QTERMWIDGET_INCLUDEDIR})
	set(QTERMWIDGET_LIBRARIES ${QTERMWIDGET_LIBRARIES})
	set(QTERMWIDGET_CFLAGS_OTHER ${QTERMWIDGET_CFLAGS_OTHER})
else()
	# Fallback: common system include and library names for qtermwidget6
	message(STATUS "pkg-config qtermwidget6 not found; using fallback include /usr/include/qtermwidget6 and library qtermwidget6")
	set(QTERMWIDGET_INCLUDE_DIRS /usr/include/qtermwidget6)
	set(QTERMWIDGET_LIBRARIES qtermwidget6)
	set(QTERMWIDGET_CFLAGS_OTHER "")
endif()

# Sources
set(GRIDTERM_SOURCES
	  main.cpp
		myterm.cpp
		myterm.hpp
		loopbackfinder.h
		loopbackfinder.cpp
)

add_executable(GridTerm ${GRIDTERM_SOURCES})

# Include directories and compile options from qtermwidget pkg-config
target_include_directories(GridTerm PRIVATE ${QTERMWIDGET_INCLUDE_DIRS})
if(QTERMWIDGET_CFLAGS_OTHER)
	target_compile_options(GridTerm PRIVATE ${QTERMWIDGET_CFLAGS_OTHER})
endif()

# Link Qt6 and qtermwidget
target_link_libraries(GridTerm
	PRIVATE
	  Qt6::Widgets
		Qt6::Core
		Qt6::Gui
		${QTERMWIDGET_LIBRARIES}
)

# Define configurable runtime arguments (CMake cache variables)
set(GRIDTERM_CWD "~/code/core/pay2exchange-core" CACHE STRING "Working directory for GridTerm")
set(GRIDTERM_BLOCKCHAIN "ec3" CACHE STRING "Blockchain type")
set(GRIDTERM_MYNODE "1" CACHE STRING "Main node number")
set(GRIDTERM_NODES_WIT "2" CACHE STRING "Number of witness nodes")
set(GRIDTERM_NODES_USER "1" CACHE STRING "Number of user nodes")

# Runtime target using configurable arguments
add_custom_target(run
	  COMMAND GridTerm ${GRIDTERM_CWD} ${GRIDTERM_BLOCKCHAIN} ${GRIDTERM_MYNODE} ${GRIDTERM_NODES_WIT} ${GRIDTERM_NODES_USER}
		DEPENDS GridTerm
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Running GridTerm with: ${GRIDTERM_CWD} ${GRIDTERM_BLOCKCHAIN} ${GRIDTERM_MYNODE} ${GRIDTERM_NODES_WIT} ${GRIDTERM_NODES_USER}"
)

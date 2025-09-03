cmake_minimum_required(VERSION 3.16)
project(QtQuitButton LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(QtQuitButton main.cpp
    myterm.h myterm.cpp)

# QTermWidget5 headers are in /usr/include/qtermwidget5
target_include_directories(QtQuitButton PRIVATE /usr/include/qtermwidget5)

# Link against the library
target_link_libraries(QtQuitButton PRIVATE Qt5::Widgets qtermwidget5)

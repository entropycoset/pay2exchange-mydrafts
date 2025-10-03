cmake_minimum_required(VERSION 3.16)
project(GridTerm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)  # needed for moc includes
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt5 REQUIRED COMPONENTS Core)


add_executable(GridTerm
    main.cpp
    myterm.cpp
    myterm.h
)

target_include_directories(GridTerm PRIVATE /usr/include/qtermwidget5)
target_link_libraries(GridTerm PRIVATE Qt5::Widgets Qt5::Core qtermwidget5)

add_custom_target(run
    COMMAND GridTerm ~/code/core/pay2exchange-core ec1 1 5 3
    DEPENDS GridTerm
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

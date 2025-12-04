cmake_minimum_required(VERSION 3.18)   # Debian 11 default
project(sockabstr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -O2)
add_compile_options(-Wall -Wextra -pedantic -O2)

add_executable(myapp server.cpp)
target_link_libraries(myapp PRIVATE pthread)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE pthread)

add_custom_target(copy_test1 ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/test1.sh
            ${CMAKE_BINARY_DIR}/test1.sh
    COMMENT "Copying test1.sh to build directory"
)

add_custom_target(run
    COMMAND ./test1.sh
    DEPENDS myapp client copy_test1
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

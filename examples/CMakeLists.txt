cmake_minimum_required(VERSION 3.22)

macro(add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE mcpxx)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endmacro()

add_example(echo_server          echo_server/main.cpp)
add_example(filesystem_server    filesystem_server/main.cpp)
add_example(prompt_server        prompt_server/main.cpp)
add_example(full_featured_server full_featured_server/main.cpp)
add_example(client_example       client_example/main.cpp)

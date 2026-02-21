cmake_minimum_required(VERSION 3.22)

macro(add_mcpxx_bench name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE mcpxx benchmark::benchmark benchmark::benchmark_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endmacro()

add_mcpxx_bench(bench_codec        bench_codec.cpp)
add_mcpxx_bench(bench_dispatch     bench_dispatch.cpp)
add_mcpxx_bench(bench_stdio_throughput bench_stdio_throughput.cpp)
add_mcpxx_bench(bench_e2e_tool_call bench_e2e_tool_call.cpp)

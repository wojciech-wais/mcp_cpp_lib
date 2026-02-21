cmake_minimum_required(VERSION 3.22)
project(mcpxx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(MCPXX_BUILD_TESTS       "Build unit + integration tests"   ON)
option(MCPXX_BUILD_BENCHMARKS  "Build performance benchmarks"     OFF)
option(MCPXX_BUILD_EXAMPLES    "Build example servers"            ON)
option(MCPXX_BUILD_PYTHON      "Build Python bindings"            OFF)
option(MCPXX_BUILD_FUZZ        "Build fuzz targets (Clang only)"  OFF)
option(MCPXX_COVERAGE          "Enable code coverage"             OFF)
option(MCPXX_SANITIZERS        "Enable ASan + UBSan"              OFF)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(MCPXX_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
    if(MCPXX_COVERAGE)
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
endif()

include(FetchContent)

# simdjson
FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.11.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(simdjson)

# nlohmann/json
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(httplib)

# spdlog (optional)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# Core library
add_library(mcpxx
    src/types.cpp
    src/json_rpc.cpp
    src/codec.cpp
    src/session.cpp
    src/router.cpp
    src/server.cpp
    src/client.cpp
    src/transport/stdio_transport.cpp
    src/transport/http_transport.cpp
)

target_include_directories(mcpxx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcpxx
    PUBLIC
        nlohmann_json::nlohmann_json
        simdjson
    PRIVATE
        httplib::httplib
        spdlog::spdlog
)

target_compile_definitions(mcpxx
    PRIVATE
        CPPHTTPLIB_OPENSSL_SUPPORT=0
)

# Install targets
install(TARGETS mcpxx
    EXPORT mcpxxTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/mcp DESTINATION include)

if(MCPXX_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MCPXX_BUILD_BENCHMARKS)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()

if(MCPXX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MCPXX_BUILD_PYTHON)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(pybind11)
    add_subdirectory(python)
endif()

cmake_minimum_required(VERSION 3.22)

include(GoogleTest)

# Helper macro to add a unit test
macro(add_mcpxx_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE mcpxx GTest::gtest_main GTest::gmock_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    gtest_discover_tests(${name})
endmacro()

# Unit tests
add_mcpxx_test(test_codec         unit/test_codec.cpp)
add_mcpxx_test(test_json_rpc      unit/test_json_rpc.cpp)
add_mcpxx_test(test_types         unit/test_types.cpp)
add_mcpxx_test(test_session       unit/test_session.cpp)
add_mcpxx_test(test_router        unit/test_router.cpp)
add_mcpxx_test(test_server        unit/test_server.cpp)
add_mcpxx_test(test_client        unit/test_client.cpp)
add_mcpxx_test(test_stdio_transport  unit/test_stdio_transport.cpp)
add_mcpxx_test(test_pagination    unit/test_pagination.cpp)

# Integration tests
add_mcpxx_test(test_stdio_lifecycle  integration/test_stdio_lifecycle.cpp)
add_mcpxx_test(test_tools_e2e       integration/test_tools_e2e.cpp)
add_mcpxx_test(test_resources_e2e   integration/test_resources_e2e.cpp)
add_mcpxx_test(test_prompts_e2e     integration/test_prompts_e2e.cpp)
add_mcpxx_test(test_cancellation    integration/test_cancellation.cpp)
add_mcpxx_test(test_progress        integration/test_progress.cpp)

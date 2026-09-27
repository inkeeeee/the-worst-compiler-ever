if(NOT GRAPH_GLIBCXX_DEBUG)
    find_package(GTest CONFIG QUIET)
endif()
if(GRAPH_GLIBCXX_DEBUG OR NOT GTest_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_library(common_tests_utils
    common-tests-utils/src/test_helpers.cpp
)
target_include_directories(common_tests_utils PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/common-tests-utils/include"
)
target_link_libraries(common_tests_utils PUBLIC graph_tools)

add_executable(graph_tests
    tool-tests/src/basic_tests.cpp
    tool-tests/src/graph_tests.cpp
    tool-tests/src/tree_tests.cpp
    tool-tests/src/lifetime_tests.cpp
    tool-tests/src/payload_tests.cpp
    tool-tests/src/exception_tests.cpp
)
target_include_directories(graph_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tool-tests/include")
target_link_libraries(graph_tests PRIVATE graph_tools common_tests_utils GTest::gtest_main)
gtest_discover_tests(graph_tests)

add_executable(frontend_tests
    frontend-tests/src/frontend_tests.cpp
    common-tests-utils/src/frontend_test_helpers.cpp
)
target_include_directories(frontend_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/frontend-tests/include")
target_compile_definitions(frontend_tests PRIVATE
    FRONTEND_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/frontend-tests/testcase"
    FRONTEND_EXAMPLE_PATH="${PROJECT_SOURCE_DIR}/example/frontend.pickme"
)
target_link_libraries(frontend_tests PRIVATE frontend_lib common_tests_utils GTest::gtest_main)
gtest_discover_tests(frontend_tests)

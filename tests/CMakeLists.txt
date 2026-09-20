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

add_executable(graph_tests
    src/basic_tests.cpp
    src/graph_tests.cpp
    src/tree_tests.cpp
    src/lifetime_tests.cpp
    src/payload_tests.cpp
    src/exception_tests.cpp
)
target_include_directories(graph_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(graph_tests PRIVATE graph_tools GTest::gtest_main)
gtest_discover_tests(graph_tests)

cmake_minimum_required(VERSION 3.20)
project(graph_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GRAPH_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GRAPH_GLIBCXX_DEBUG "Enable GCC standard library iterator diagnostics" OFF)

if(GRAPH_GLIBCXX_DEBUG)
    add_compile_definitions(_GLIBCXX_DEBUG)
endif()

add_subdirectory(tools)
add_subdirectory(example)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

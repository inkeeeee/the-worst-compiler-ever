find_package(BISON 3.8 REQUIRED)
find_package(FLEX 2.6 REQUIRED)

set(FRONTEND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FRONTEND_GENERATED_DIR}")

BISON_TARGET(FrontendParser
    "${CMAKE_CURRENT_SOURCE_DIR}/grammar/parser.y"
    "${FRONTEND_GENERATED_DIR}/parser.cpp"
    DEFINES_FILE "${FRONTEND_GENERATED_DIR}/parser.hpp"
    COMPILE_FLAGS "-Wall -Werror=conflicts-sr -Werror=conflicts-rr"
)
FLEX_TARGET(FrontendLexer
    "${CMAKE_CURRENT_SOURCE_DIR}/grammar/lexer.l"
    "${FRONTEND_GENERATED_DIR}/lexer.cpp"
)
ADD_FLEX_BISON_DEPENDENCY(FrontendLexer FrontendParser)

add_library(frontend_lib
    src/ast.cpp
    src/frontend.cpp
    src/lexer.cpp
    src/source.cpp
    ${BISON_FrontendParser_OUTPUTS}
    ${FLEX_FrontendLexer_OUTPUTS}
)
target_include_directories(frontend_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${FRONTEND_GENERATED_DIR}"
)
target_include_directories(frontend_lib SYSTEM PRIVATE ${FLEX_INCLUDE_DIRS})
target_link_libraries(frontend_lib PUBLIC graph_tools)

add_executable(frontend src/main.cpp)
target_link_libraries(frontend PRIVATE frontend_lib)

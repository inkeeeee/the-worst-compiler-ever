add_library(hir_lib src/hir.cpp src/verify.cpp src/lower.cpp)
target_include_directories(hir_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hir_lib PUBLIC frontend_lib graph_tools)

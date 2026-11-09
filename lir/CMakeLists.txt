add_library(lir_lib src/lir.cpp src/layout.cpp src/lower.cpp src/verify.cpp)
target_include_directories(lir_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(lir_lib PUBLIC hir_lib)

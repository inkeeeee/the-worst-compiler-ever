add_library(codegen_lib src/codegen.cpp src/registers.cpp)
target_include_directories(codegen_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(codegen_lib PUBLIC lir_lib)

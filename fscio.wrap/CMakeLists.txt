add_contract(fscio.wrap fscio.wrap ${CMAKE_CURRENT_SOURCE_DIR}/src/fscio.wrap.cpp)
target_include_directories(fscio.wrap.wasm
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(fscio.wrap.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

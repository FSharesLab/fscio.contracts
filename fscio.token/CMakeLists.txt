add_contract(fscio.token fscio.token ${CMAKE_CURRENT_SOURCE_DIR}/src/fscio.token.cpp)
target_include_directories(fscio.token.wasm
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(fscio.token.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_contract(fscio.msig fscio.msig ${CMAKE_CURRENT_SOURCE_DIR}/src/fscio.msig.cpp)
target_include_directories(fscio.msig.wasm
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(fscio.msig.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

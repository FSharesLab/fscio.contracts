add_contract(fscio.system fscio.system ${CMAKE_CURRENT_SOURCE_DIR}/src/fscio.system.cpp)
#add_executable(fscio.system.wasm ${CMAKE_CURRENT_SOURCE_DIR}/src/fscio.system.cpp)
target_include_directories(fscio.system.wasm
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../fscio.token/include)

set_target_properties(fscio.system.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

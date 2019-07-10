cmake_minimum_required(VERSION 3.5)
project(fscio_contracts VERSION 1.0.1)

set(FSCIO_CDT_VERSION_MIN "1.0")
set(FSCIO_CDT_VERSION_SOFT_MAX "1.0")
#set(FSCIO_CDT_VERSION_HARD_MAX "")

find_package(fscio.cdt)

### Check the version of fscio.cdt
set(VERSION_MATCH_ERROR_MSG "")
FSCIO_CHECK_VERSION(VERSION_OUTPUT "${FSCIO_CDT_VERSION}"
                                   "${FSCIO_CDT_VERSION_MIN}"
                                   "${FSCIO_CDT_VERSION_SOFT_MAX}"
                                   "${FSCIO_CDT_VERSION_HARD_MAX}"
                                   VERSION_MATCH_ERROR_MSG)
if(VERSION_OUTPUT STREQUAL "MATCH")
   message(STATUS "Using fscio.cdt version ${FSCIO_CDT_VERSION}")
elseif(VERSION_OUTPUT STREQUAL "WARN")
   message(WARNING "Using fscio.cdt version ${FSCIO_CDT_VERSION} even though it exceeds the maximum supported version of ${FSCIO_CDT_VERSION_SOFT_MAX}; continuing with configuration, however build may fail.\nIt is recommended to use fscio.cdt version ${FSCIO_CDT_VERSION_SOFT_MAX}.x")
else() # INVALID OR MISMATCH
   message(FATAL_ERROR "Found fscio.cdt version ${FSCIO_CDT_VERSION} but it does not satisfy version requirements: ${VERSION_MATCH_ERROR_MSG}\nPlease use fscio.cdt version ${FSCIO_CDT_VERSION_SOFT_MAX}.x")
endif(VERSION_OUTPUT STREQUAL "MATCH")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(TEST_BUILD_TYPE "Debug")
   set(CMAKE_BUILD_TYPE "Release")
else()
   set(TEST_BUILD_TYPE ${CMAKE_BUILD_TYPE})
endif()

add_subdirectory(fscio.msig)
add_subdirectory(fscio.wrap)
add_subdirectory(fscio.system)
add_subdirectory(fscio.token)

if (APPLE)
   set(OPENSSL_ROOT "/usr/local/opt/openssl")
elseif (UNIX)
   set(OPENSSL_ROOT "/usr/include/openssl")
endif()
set(SECP256K1_ROOT "/usr/local")


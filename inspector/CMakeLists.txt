cmake_minimum_required(VERSION 3.13)
project(inspector)

message("inspector")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0063 NEW)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(INSPECTOR_COMMON_DIR common)
set(INSPECTOR_CLIENT_DIR client/src)


if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND INSPECTOR_COMPILE_OPTIONS -Werror -Wall -Wextra -Weffc++ -Wconversion -pedantic -Werror=return-type -Wno-unused-command-line-argument)
endif ()

list(APPEND INSPECTOR_CLIENT_SRC
        ${INSPECTOR_COMMON_DIR}
        ${INSPECTOR_CLIENT_DIR})
file(GLOB INSPECTOR_CLIENT_FILES
        ${INSPECTOR_COMMON_DIR}/*.*
        ${INSPECTOR_CLIENT_DIR}/*.*)

add_library(inspector STATIC ${INSPECTOR_CLIENT_FILES})
target_include_directories(inspector PUBLIC ${INSPECTOR_CLIENT_SRC})
target_compile_options(inspector PUBLIC ${INSPECTOR_COMPILE_OPTIONS})

set(CommonIncludes
    ${INSPECTOR_COMMON_DIR}/Alloc.h
    ${INSPECTOR_COMMON_DIR}/Protocol.h
    ${INSPECTOR_COMMON_DIR}/Queue.h
    ${INSPECTOR_COMMON_DIR}/Singleton.h
    ${INSPECTOR_COMMON_DIR}/Socket.h
    ${INSPECTOR_COMMON_DIR}/Utils.h
    ${INSPECTOR_COMMON_DIR}/Yield.h)

install(FILES ${CommonIncludes}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/inspector/common)

set(ClientIncludes
    ${INSPECTOR_CLIENT_DIR}/FastVector.h
    ${INSPECTOR_CLIENT_DIR}/Inspector.h
    ${INSPECTOR_CLIENT_DIR}/Scoped.h
    ${INSPECTOR_CLIENT_DIR}/Thread.h)

install(FILES ${ClientIncludes}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/inspector/client/src)
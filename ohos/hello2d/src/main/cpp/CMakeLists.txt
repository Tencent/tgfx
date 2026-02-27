# the minimum version of CMake.
cmake_minimum_required(VERSION 3.4.1)
project(Hello2D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-unused-command-line-argument)
endif ()

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

find_library(NATIVE_VSYNC_LIB native_vsync)
set(TGFX_BUILD_HELLO2D ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../ ./tgfx)

file(GLOB_RECURSE HELLO_2D_FILES *.cpp)
add_library(hello2d SHARED ${HELLO_2D_FILES})
target_link_libraries(hello2d tgfx-hello2d tgfx ${TGFX_SHARED_LIBS} ${NATIVE_VSYNC_LIB})

# the minimum version of CMake.
cmake_minimum_required(VERSION 3.4.1)

set(OHOS_SDK_DIR "/Users/runner/setup-ohos-sdk/darwin/11/native/sysroot/usr/lib/aarch64-linux-ohos")

set(IMAGE_SOURCE_LIB "${OHOS_SDK_DIR}/libimage_source_ndk.z.so")
set(PIXELMAP_LIB    "${OHOS_SDK_DIR}/libpixelmap_ndk.z.so")

if (NOT EXISTS "${IMAGE_SOURCE_LIB}")
    message(FATAL_ERROR "Missing ${IMAGE_SOURCE_LIB}")
endif()
if (NOT EXISTS "${PIXELMAP_LIB}")
    message(FATAL_ERROR "Missing ${PIXELMAP_LIB}")
endif()

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
set(TGFX_BUILD_DRAWERS ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../ ./tgfx)

file(GLOB_RECURSE HELLO_2D_FILES *.cpp)
add_library(hello2d SHARED ${HELLO_2D_FILES})
target_link_libraries(hello2d tgfx-drawers tgfx ${TGFX_SHARED_LIBS} ${NATIVE_VSYNC_LIB})
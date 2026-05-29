cmake_minimum_required(VERSION 3.13)
#set(CMAKE_VERBOSE_MAKEFILE ON)
project(Hello2D)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/check_emscripten.cmake)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../hello2d/include)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

set(TGFX_BUILD_HELLO2D ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ ./tgfx)

file(GLOB_RECURSE HELLO_2D_FILES src/*.cpp)

if (DEFINED EMSCRIPTEN)
    add_executable(hello2d ${HELLO_2D_FILES})
    list(APPEND H2D_COMPILE_OPTIONS -fno-rtti -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
    if (TGFX_USE_WEBGPU)
        set(H2D_GRAPHICS_OPTIONS -sUSE_WEBGPU=1)
    else ()
        set(H2D_GRAPHICS_OPTIONS -sMAX_WEBGL_VERSION=2)
    endif ()
    list(APPEND H2D_LINK_OPTIONS --no-entry -lembind -fno-rtti -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0 -sEXPORT_NAME='Hello2D' -sWASM=1
            ${H2D_GRAPHICS_OPTIONS} -sEXPORTED_RUNTIME_METHODS=['GL','HEAPU8'] -sMODULARIZE=1
            -sENVIRONMENT=web,worker -sEXPORT_ES6=1)
    if (EMSCRIPTEN_PTHREADS)
        list(APPEND H2D_LINK_OPTIONS -sUSE_PTHREADS=1 -sINITIAL_MEMORY=32MB -sALLOW_MEMORY_GROWTH=1
                -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency -sPROXY_TO_PTHREAD=1
                -sEXIT_RUNTIME=0 -sINVOKE_RUN=0 -sMALLOC=dlmalloc)
        list(APPEND H2D_COMPILE_OPTIONS -fPIC -pthread)
    else ()
        list(APPEND H2D_LINK_OPTIONS -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB)
    endif ()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND H2D_COMPILE_OPTIONS -O0 -g3)
        list(APPEND H2D_LINK_OPTIONS -O0 -g3 -sSAFE_HEAP=1 -Wno-limited-postlink-optimizations)
    else ()
        list(APPEND H2D_COMPILE_OPTIONS -Oz)
        list(APPEND H2D_LINK_OPTIONS -Oz)
    endif ()
else ()
    add_library(hello2d SHARED ${HELLO_2D_FILES})
endif ()

target_compile_options(hello2d PUBLIC ${H2D_COMPILE_OPTIONS})
target_link_options(hello2d PUBLIC ${H2D_LINK_OPTIONS})
if (TGFX_USE_WEBGPU)
    target_compile_definitions(hello2d PRIVATE TGFX_USE_WEBGPU ${TGFX_DEFINES})
endif ()
target_link_libraries(hello2d tgfx-hello2d tgfx ${HELLO_2D_SHARED_LIBS})

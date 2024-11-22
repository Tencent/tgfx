cmake_minimum_required(VERSION 3.13)
#set(CMAKE_VERBOSE_MAKEFILE ON)
project(Figma)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

set(TGFX_BUILD_DRAWERS ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ ./tgfx)

file(GLOB_RECURSE FIGMA_FILES cpp/*.cpp cpp/MessageParser.cpp)

if (DEFINED EMSCRIPTEN)
    add_executable(figma ${FIGMA_FILES})
    list(APPEND H2D_COMPILE_OPTIONS -fno-rtti -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
    list(APPEND H2D_LINK_OPTIONS --no-entry -lembind -fno-rtti -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0 -sEXPORT_NAME='Figma' -sWASM=1 -sASYNCIFY
            -sUSE_WEBGL2=1 -sEXPORTED_RUNTIME_METHODS=['GL','Asyncify'] -sMODULARIZE=1
            -sENVIRONMENT='web,worker' -sEXPORT_ES6=1 -sUSE_ES6_IMPORT_META=0)
    if (EMSCRIPTEN_PTHREADS)
        list(APPEND H2D_LINK_OPTIONS -sTOTAL_MEMORY=512MB -sUSE_PTHREADS=1
                -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency -sPROXY_TO_PTHREAD=1
                -sEXIT_RUNTIME=0 -sINVOKE_RUN=0
                )
        list(APPEND H2D_COMPILE_OPTIONS -fPIC -pthread)
    else()
        list(APPEND H2D_LINK_OPTIONS -sALLOW_MEMORY_GROWTH=1)
    endif ()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND H2D_COMPILE_OPTIONS -O0 -g3)
        list(APPEND H2D_LINK_OPTIONS -O0 -g3 -sSAFE_HEAP=1 -Wno-limited-postlink-optimizations)
    else ()
        list(APPEND H2D_COMPILE_OPTIONS -Oz)
        list(APPEND H2D_LINK_OPTIONS -Oz)
    endif ()
else ()
    add_library(figma SHARED ${FIGMA_FILES})
endif ()
target_include_directories(figma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/json/include)
target_compile_options(figma PUBLIC ${H2D_COMPILE_OPTIONS})
target_link_options(figma PUBLIC ${H2D_LINK_OPTIONS})
target_link_libraries(figma tgfx-drawers tgfx ${HELLO_2D_SHARED_LIBS})

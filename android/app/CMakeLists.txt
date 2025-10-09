cmake_minimum_required(VERSION 3.10)
project(Hello2D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_library(LOG_LIB log)
list(APPEND HELLO_2D_SHARED_LIBS ${LOG_LIB})
find_library(ANDROID_LIB android)
list(APPEND HELLO_2D_SHARED_LIBS ${ANDROID_LIB})

set(TGFX_BUILD_DRAWERS ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ ./tgfx)

file(GLOB_RECURSE HELLO_2D_FILES src/main/cpp/*.*)

add_library(hello2d SHARED ${HELLO_2D_FILES})
target_link_libraries(hello2d tgfx-hello2d tgfx ${HELLO_2D_SHARED_LIBS})

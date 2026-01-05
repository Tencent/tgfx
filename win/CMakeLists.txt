cmake_minimum_required(VERSION 3.13)
project(Hello2D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options("/utf-8")
    string(TOLOWER ${MSVC_C_ARCHITECTURE_ID} ARCH)
endif (MSVC)

add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES -DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
if (${CMAKE_SYSTEM_VERSION} VERSION_GREATER_EQUAL 6.3) # Windows 8.1 or later
    add_definitions(-D_WIN32_WINNT=0x0603)
    find_library(SHCORE_LIB SHCore)
    list(APPEND HELLO_2D_LIBS ${SHCORE_LIB})
else ()
    add_definitions(-D_WIN32_WINNT=0x0601) # Windows 7
    find_library(USER32 user32)
    list(APPEND HELLO_2D_LIBS ${USER32})
endif ()

if (TGFX_USE_ANGLE)
    list(APPEND HELLO_2D_INCLUDES ../vendor/angle/include)
    file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/libEGL.dll)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/angle/win/${ARCH}/libEGL.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/libGLESv2.dll)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/angle/win/${ARCH}/libGLESv2.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif ()

set(TGFX_BUILD_HELLO2D ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ ./tgfx)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DDEBUG)
else ()
    add_definitions(-DNDEBUG)
endif ()

file(GLOB_RECURSE HELLO_2D_SOURCE_FILES src/*.*)
add_executable(Hello2D WIN32 ${HELLO_2D_SOURCE_FILES} app.rc)
target_include_directories(Hello2D PRIVATE ${HELLO_2D_INCLUDES} src)
target_link_libraries(Hello2D tgfx-hello2d tgfx ${HELLO_2D_LIBS})

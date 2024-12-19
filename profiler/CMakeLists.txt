cmake_minimum_required(VERSION 3.13)
project(TGFXProfiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

#if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#    add_definitions(-Werror -Wall -Wextra -Weffc++ -Wconversion -pedantic -Werror=return-type)
#endif ()

if (MSVC)
    add_compile_options("/utf-8")
endif (MSVC)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DDEBUG)
endif ()

set(CMAKE_CXX_STANDARD 20)
add_definitions(-DQT_NO_KEYWORDS)
include(cmake/tracy.cmake)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# configures the local installation path of the QT library.
if (NOT CMAKE_PREFIX_PATH)
    if (NOT EXISTS ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg)
        file(WRITE ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg
                "set(CMAKE_PREFIX_PATH /Users/[username]/Qt/6.6.1/macos/lib/cmake)  #put your own QT path here")
    endif ()
    include("./QTCMAKE.cfg")
endif ()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if (${QT_VERSION_MAJOR} LESS 6)
    message("The QT version is less than 6.0, force to use x86_64 architecture.")
    SET(CMAKE_SYSTEM_PROCESSOR x86_64)
    SET(CMAKE_OSX_ARCHITECTURES x86_64)
endif ()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets OpenGL Qml Quick)
list(APPEND TGFX_PROFILER_LIBS Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick)
if (${QT_VERSION} VERSION_LESS "5.15")
    function(qt_add_resources outfiles)
        qt5_add_resources("${outfiles}" ${ARGN})
        if (TARGET ${outfiles})
            cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_TARGETS" "")
            if (arg_OUTPUT_TARGETS)
                set(${arg_OUTPUT_TARGETS} ${${arg_OUTPUT_TARGETS}} PARENT_SCOPE)
            endif ()
        else ()
            set("${outfiles}" "${${outfiles}}" PARENT_SCOPE)
        endif ()
    endfunction()
endif ()

if (APPLE)
    message("test")
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    list(APPEND TGFX_PROFILER_LIBS ${APPLICATION_SERVICES_FRAMEWORK})
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    list(APPEND TGFX_PROFILER_LIBS ${QUARTZ_CORE})
    find_library(COCOA Cocoa REQUIRED)
    list(APPEND TGFX_PROFILER_LIBS ${COCOA})
    find_library(FOUNDATION Foundation REQUIRED)
    list(APPEND TGFX_PROFILER_LIBS ${FOUNDATION})
    find_library(ICONV_LIBRARIES NAMES iconv libiconv libiconv-2 c)
    list(APPEND TGFX_PROFILER_LIBS ${ICONV_LIBRARIES})
    find_library(VIDEOTOOLBOX VideoToolbox)
    list(APPEND TGFX_PROFILER_LIBS ${VIDEOTOOLBOX})
    find_library(CORE_MEDIA CoreMedia)
    list(APPEND TGFX_PROFILER_LIBS ${CORE_MEDIA})
    find_library(COMPRESSION_LIBRARIES NAMES compression)
    list(APPEND TGFX_PROFILER_LIBS ${COMPRESSION_LIBRARIES})
elseif (WIN32)
    set(BUILD_USE_64BITS ON)
    add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES)
    find_library(Bcrypt_LIB Bcrypt)
    list(APPEND TGFX_PROFILER_LIBS ${Bcrypt_LIB})
    find_library(ws2_32_LIB ws2_32)
    list(APPEND TGFX_PROFILER_LIBS ${ws2_32_LIB})
endif ()

#set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

list(APPEND TGFX_PROFILER_INCLUDE
        profiler/include
        ${TRACY_PATH})


file(GLOB TGFX_PROFILER_FILES
        profiler/main.cpp
        profiler/src/ProfilerApplication.cpp
        profiler/src/MainView.cpp
        profiler/src/ToolView.cpp
        profiler/src/View.cpp
        profiler/src/TimelineView.cpp
        profiler/src/TimelineItem.cpp
        profiler/src/TimelineItemThread.cpp
        profiler/src/FramesView.cpp
        profiler/src/ProfilerWindow.cpp)

add_executable(Profiler ${RC_FILES} ${TGFX_PROFILER_FILES} ${SERVER_FILES} ${PROFILER_FILES} ${COMMON_FILES})
target_include_directories(Profiler PRIVATE ${TGFX_PROFILER_INCLUDE})
target_link_libraries(Profiler ${TGFX_PROFILER_LIBS} ${TRACY_LIBS})
target_link_options(Profiler PRIVATE ${TRACY_OPTION})
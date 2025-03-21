cmake_minimum_required(VERSION 3.13)
project(TGFXProfiler)

include(../third_party/vendor_tools/vendor.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

if (MSVC)
    add_compile_options("/utf-8")
endif (MSVC)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DDEBUG)
endif ()

list(APPEND TGFX_STATIC_VENDORS tracyServer)
list(APPEND TGFX_PROFILER_INCLUDE
        ../third_party/tracy/server
        ../third_party/tracy/profiler
        ../third_party/tracy/public/common)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(TGFX_USE_QT ON)

# configures the local installation path of the QT library.
if (NOT CMAKE_PREFIX_PATH)
    if (NOT EXISTS ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg)
        file(WRITE ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg
                "set(CMAKE_PREFIX_PATH /Users/[username]/Qt/6.6.1/macos/lib/cmake)  #put your own QT path here")
    endif ()
    include("./QTCMAKE.cfg")
endif ()

list(APPEND PROFILER_COMPILE_OPTIONS -Wno-error)

option(TGFX_ENABLE_PROFILING "Enable Profiling" OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ ./tgfx)

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

qt_add_resources(QT_RESOURCES res.qrc)

if (APPLE)
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

list(APPEND TGFX_PROFILER_SRC src)
file(GLOB TGFX_PROFILER_FILES src/*.*)

find_vendor_libraries(tgfx-vendor STATIC TGFX_VENDOR_STATIC_LIBRARIES)
list(APPEND TGFX_PROFILER_STATIC_LIBS ${TGFX_VENDOR_STATIC_LIBRARIES})

add_executable(Profiler ${RC_FILES} ${TGFX_PROFILER_FILES} ${QT_RESOURCES})
add_dependencies(Profiler tgfx-vendor)
target_include_directories(Profiler PUBLIC ${TGFX_PROFILER_SRC})
target_include_directories(Profiler SYSTEM PRIVATE ${TGFX_PROFILER_INCLUDE})
target_link_libraries(Profiler tgfx ${TGFX_PROFILER_LIBS})
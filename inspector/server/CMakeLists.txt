cmake_minimum_required(VERSION 3.13)
project(inspectorTool)

include(./third_party/vendor_tools/vendor.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0063 NEW)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(INSPECTOR_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(ZSTD_DIR third_party/zstd)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(TGFX_USE_QT ON)

if (NOT CMAKE_PREFIX_PATH)
    if (NOT EXISTS ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg)
        file(WRITE ${PROJECT_SOURCE_DIR}/QTCMAKE.cfg
                "set(CMAKE_PREFIX_PATH /Users/[username]/Qt/6.6.1/macos/lib/cmake)  #put your own QT path here")
    endif ()
    include("QTCMAKE.cfg")
endif ()

string(REGEX MATCH "([0-9]+)\\.[0-9]+\\.[0-9]+" QT_VERSION ${CMAKE_PREFIX_PATH})
if (QT_VERSION)
    string(REGEX MATCH "^[0-9]+" QT_VERSION_MAJOR ${QT_VERSION})
    if (QT_VERSION_MAJOR GREATER_EQUAL 6 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "QT has dropped support for 32-bit builds since version 6.0.0")
    endif ()
endif ()

option(TGFX_USE_INSPECTOR "Enable profiling support for the TGFX Inspector tool" OFF)
add_subdirectory(../.. ./tgfx)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if (${QT_VERSION_MAJOR} LESS 6)
    message("The QT version is less than 6.0, force to use x86_64 architecture.")
    SET(CMAKE_SYSTEM_PROCESSOR x86_64)
    SET(CMAKE_OSX_ARCHITECTURES x86_64)
endif ()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets OpenGL qml Quick WebSockets Network QuickControls2 Core5Compat)
list(APPEND TGFX_INSPECTOR_LIBS Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::WebSockets Qt${QT_VERSION_MAJOR}::OpenGL Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick
        Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::QuickControls2 Qt${QT_VERSION_MAJOR}::Core5Compat)
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
    list(APPEND TGFX_INSPECTOR_LIBS ${APPLICATION_SERVICES_FRAMEWORK})
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    list(APPEND TGFX_INSPECTOR_LIBS ${QUARTZ_CORE})
    find_library(COCOA Cocoa REQUIRED)
    list(APPEND TGFX_INSPECTOR_LIBS ${COCOA})
    find_library(FOUNDATION Foundation REQUIRED)
    list(APPEND TGFX_INSPECTOR_LIBS ${FOUNDATION})
    find_library(ICONV_LIBRARIES NAMES iconv libiconv libiconv-2 c)
    list(APPEND TGFX_INSPECTOR_LIBS ${ICONV_LIBRARIES})
    find_library(VIDEOTOOLBOX VideoToolbox)
    list(APPEND TGFX_INSPECTOR_LIBS ${VIDEOTOOLBOX})
    find_library(CORE_MEDIA CoreMedia)
    list(APPEND TGFX_INSPECTOR_LIBS ${CORE_MEDIA})
    find_library(COMPRESSION_LIBRARIES NAMES compression)
    list(APPEND TGFX_INSPECTOR_LIBS ${COMPRESSION_LIBRARIES})
elseif (WIN32)
    set(BUILD_USE_64BITS ON)
    add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES)
    find_library(Bcrypt_LIB Bcrypt)
    list(APPEND TGFX_INSPECTOR_LIBS ${Bcrypt_LIB})
    find_library(ws2_32_LIB ws2_32)
    list(APPEND TGFX_INSPECTOR_LIBS ${ws2_32_LIB})
endif ()

list(APPEND TGFX_INSPECTOR_SRC src src/tags src/socket/*.* src/layerInspector/*.*)
file(GLOB TGFX_INSPECTOR_FILES src/*.* src/tags/*.* src/socket/*.* src/layerInspector/*.*)

#list(APPEND TGFX_INSPECTOR_INCLUDE
#        ${ZSTD_DIR}/zstd_ddict.h
#        ${ZSTD_DIR}/zstd.h
#        ${ZSTD_DIR}/zstd_decompress_block.h
#        ${ZSTD_DIR}/zstd_decompress_internal.h)

file(GLOB INSPECTOR_DECOMPRESS_FILES
        ${ZSTD_DIR}/huf_decompress.c
        ${ZSTD_DIR}/zstd_ddict.c
        ${ZSTD_DIR}/zstd_decompress.c
        ${ZSTD_DIR}/zstd_decompress_block.c)

list(APPEND TGFX_INSPECTOR_COMMON_SRC ${INSPECTOR_COMMON_DIR})
file(GLOB TGFX_INSPECTOR_COMMON_FILES ${INSPECTOR_COMMON_DIR}/*.*)

list(APPEND INSPECTOR_STATIC_VENDORS KDDockWidgets flatbuffers)
add_vendor_target(inspector-vendor STATIC_VENDORS ${INSPECTOR_STATIC_VENDORS})
find_vendor_libraries(inspector-vendor STATIC INSPECTOR_VENDOR_STATIC_LIBRARIES)
list(APPEND TGFX_INSPECTOR_LIBS ${INSPECTOR_VENDOR_STATIC_LIBRARIES})
list(APPEND TGFX_INSPECTOR_DEFINE KDDW_FRONTEND_QTQUICK KDDW_FRONTEND_QT KDDW_QTGUI_TYPES)

list(APPEND TGFX_INSPECTOR_INCLUDE ./third_party/KDDockWidgets/src/fwd_headers ./third_party/flatbuffers/include)

add_executable(inspectorTool ${RC_FILES} ${TGFX_INSPECTOR_FILES} ${TGFX_INSPECTOR_COMMON_FILES} ${QT_RESOURCES})
target_include_directories(inspectorTool PUBLIC ${TGFX_INSPECTOR_SRC} ${TGFX_INSPECTOR_COMMON_SRC})
target_include_directories(inspectorTool SYSTEM PRIVATE ${TGFX_INSPECTOR_INCLUDE})
target_compile_definitions(inspectorTool PUBLIC ${TGFX_INSPECTOR_DEFINE})
target_link_libraries(inspectorTool tgfx ${TGFX_INSPECTOR_LIBS})
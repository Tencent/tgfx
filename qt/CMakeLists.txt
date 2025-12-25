cmake_minimum_required(VERSION 3.13)
project(Hello2D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_definitions(-Werror -Wall -Wextra -Weffc++ -Wconversion -pedantic -Werror=return-type)
endif ()

if (MSVC)
    add_compile_options("/utf-8")
endif (MSVC)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DDEBUG)
endif ()

add_definitions(-DQT_NO_KEYWORDS)

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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL Qml Quick)
if (Qt6_VERSION VERSION_LESS "6.2")
    message(FATAL_ERROR "Qt6 version 6.2 or higher is required. Found: ${Qt6_VERSION}")
endif ()
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Qt has dropped support for 32-bit builds since version 6.0.0")
endif ()
list(APPEND HELLO_2D_LIBS Qt6::Core Qt6::Widgets
        Qt6::OpenGL Qt6::Qml Qt6::Quick)

qt_add_resources(QT_RESOURCES res.qrc)

if (APPLE)
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    list(APPEND HELLO_2D_LIBS ${APPLICATION_SERVICES_FRAMEWORK})
    find_library(QUARTZ_CORE QuartzCore REQUIRED)
    list(APPEND HELLO_2D_LIBS ${QUARTZ_CORE})
    find_library(COCOA Cocoa REQUIRED)
    list(APPEND HELLO_2D_LIBS ${COCOA})
    find_library(FOUNDATION Foundation REQUIRED)
    list(APPEND HELLO_2D_LIBS ${FOUNDATION})
    find_library(ICONV_LIBRARIES NAMES iconv libiconv libiconv-2 c)
    list(APPEND HELLO_2D_LIBS ${ICONV_LIBRARIES})
    find_library(VIDEOTOOLBOX VideoToolbox)
    list(APPEND HELLO_2D_LIBS ${VIDEOTOOLBOX})
    find_library(CORE_MEDIA CoreMedia)
    list(APPEND HELLO_2D_LIBS ${CORE_MEDIA})
    find_library(COMPRESSION_LIBRARIES NAMES compression)
    list(APPEND HELLO_2D_LIBS ${COMPRESSION_LIBRARIES})
elseif (WIN32)
    set(BUILD_USE_64BITS ON)
    add_definitions(-DNOMINMAX -D_USE_MATH_DEFINES)
    find_library(Bcrypt_LIB Bcrypt)
    list(APPEND HELLO_2D_LIBS ${Bcrypt_LIB})
    find_library(ws2_32_LIB ws2_32)
    list(APPEND HELLO_2D_LIBS ${ws2_32_LIB})
endif ()

set(TGFX_USE_QT ON)
set(TGFX_BUILD_HELLO2D ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ ./tgfx)

set(HELLO_2D_INCLUDES src)
file(GLOB_RECURSE HELLO_2D_SOURCE_FILES src/*.*)
add_executable(Hello2D ${RC_FILES} ${HELLO_2D_SOURCE_FILES} ${QT_RESOURCES})
target_include_directories(Hello2D PRIVATE ${HELLO_2D_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/../hello2d/include)
target_link_libraries(Hello2D tgfx-hello2d tgfx ${HELLO_2D_LIBS})
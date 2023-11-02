cmake_minimum_required(VERSION 3.10)
project(TGFXDemo)

set(TGFX_DEMO_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include)

find_library(LOG_LIB log)
list(APPEND TGFX_DEMO_SHARED_LIBS ${LOG_LIB})
find_library(ANDROID_LIB android)
list(APPEND TGFX_DEMO_SHARED_LIBS ${ANDROID_LIB})

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../ ./tgfx)

add_library(tgfxdemo SHARED JTGFXView.cpp)
target_include_directories(tgfxdemo PRIVATE ${TGFX_DEMO_INCLUDES})
target_link_libraries(tgfxdemo tgfx ${TGFX_DEMO_SHARED_LIBS})

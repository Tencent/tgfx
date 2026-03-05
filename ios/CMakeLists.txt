cmake_minimum_required(VERSION 3.13)
project(Hello2D)
enable_language(OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TGFX_BUILD_HELLO2D ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ./tgfx)

if (TGFX_USE_METAL)
    set(TGFX_VIEW_DIR Hello2D/metal)
    find_library(MetalKit_LIBS MetalKit REQUIRED)
else ()
    set(TGFX_VIEW_DIR Hello2D/opengl)
endif ()

file(GLOB HELLO2D_SOURCES Hello2D/*.m Hello2D/*.mm ${TGFX_VIEW_DIR}/*.mm)
file(GLOB HELLO2D_HEADERS Hello2D/*.h ${TGFX_VIEW_DIR}/*.h)

set(HELLO2D_STORYBOARDS
        Hello2D/Base.lproj/LaunchScreen.storyboard
        Hello2D/Base.lproj/Main.storyboard)
set(HELLO2D_RESOURCES ${HELLO2D_STORYBOARDS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/bridge.jpg
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/tgfx.png)

add_executable(Hello2D MACOSX_BUNDLE
        ${HELLO2D_SOURCES} ${HELLO2D_HEADERS}
        ${HELLO2D_RESOURCES} Hello2D/Assets.xcassets)

set_target_properties(Hello2D PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Hello2D/Info.plist
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.tgfx.hello2d
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM A25BQKLXTS
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_SDKROOT iphoneos
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME AccentColor)

set_source_files_properties(Hello2D/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(${HELLO2D_STORYBOARDS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/bridge.jpg
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/tgfx.png
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_compile_definitions(Hello2D PRIVATE ${TGFX_DEFINES})
target_include_directories(Hello2D PRIVATE ../include ../hello2d/include ${TGFX_VIEW_DIR})

if (TGFX_USE_METAL)
    target_link_libraries(Hello2D tgfx-hello2d tgfx ${TGFX_SHARED_LIBS} ${MetalKit_LIBS})
else ()
    target_link_libraries(Hello2D tgfx-hello2d tgfx ${TGFX_SHARED_LIBS})
endif ()

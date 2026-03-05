cmake_minimum_required(VERSION 3.13)
project(Hello2D)
enable_language(OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TGFX_BUILD_HELLO2D ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ./tgfx)

file(GLOB HELLO2D_SOURCES Hello2D/*.m Hello2D/*.mm)
file(GLOB HELLO2D_HEADERS Hello2D/*.h)

set(HELLO2D_STORYBOARDS Hello2D/Base.lproj/Main.storyboard)
set(HELLO2D_RESOURCES ${HELLO2D_STORYBOARDS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/bridge.jpg
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/tgfx.png)

add_executable(Hello2D MACOSX_BUNDLE
        ${HELLO2D_SOURCES} ${HELLO2D_HEADERS}
        ${HELLO2D_RESOURCES} Hello2D/Assets.xcassets)

set_target_properties(Hello2D PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Hello2D/Info.plist
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.tgfx.hello2d
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/Hello2D/Hello2D.entitlements
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_COMBINE_HIDPI_IMAGES YES)

set_source_files_properties(Hello2D/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(${HELLO2D_STORYBOARDS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/bridge.jpg
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources/assets/tgfx.png
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_compile_definitions(Hello2D PRIVATE ${TGFX_DEFINES})
target_include_directories(Hello2D PRIVATE ../include ../hello2d/include)
target_link_libraries(Hello2D tgfx-hello2d tgfx ${TGFX_SHARED_LIBS})

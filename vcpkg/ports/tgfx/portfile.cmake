vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO Tencent/tgfx
        REF dd6457d2a77ee0e557cd451c9ebe83a9790fdee8
        SHA512 2e952a9f4d26b92346f05db3be54c95c13df7ed5f93fb771767a5c5504c65896383d5b5bda6466cea29124d97ec0b95ef18aa6525c16a6499a3f7009088a1a22
)

# Local compilation and debugging of tgfx source code
# set(SOURCE_PATH "/path/to/your/tgfx")

include("${CMAKE_CURRENT_LIST_DIR}/tgfx-functions.cmake")

vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")

if(VCPKG_TARGET_IS_WINDOWS)
    set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)
endif()
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)

find_program(NODEJS
    NAMES node
    PATHS
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node"
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node/bin"
    ENV PATH
    NO_DEFAULT_PATH
)
if(NOT NODEJS)
    message(FATAL_ERROR "node not found! Please install it via your system package manager!")
endif()

get_filename_component(NODEJS_DIR "${NODEJS}" DIRECTORY )
vcpkg_add_to_path(PREPEND "${NODEJS_DIR}")

find_program(NPM
    NAMES npm.cmd npm
    PATHS
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node"
        "${CURRENT_HOST_INSTALLED_DIR}/tools/node/bin"
    ENV PATH
    NO_DEFAULT_PATH
)
if(NOT NPM)
    message(FATAL_ERROR "npm not found! Please install it via your system package manager!")
endif()

message(STATUS "Installing depsync via npm...")
vcpkg_execute_required_process(
    COMMAND ${NPM} install depsync -g
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "npm-install-depsync"
)

find_program(NINJA
        NAMES ninja
        PATHS
        "${CURRENT_HOST_INSTALLED_DIR}/tools/ninja"
        ENV PATH
        NO_DEFAULT_PATH
)
if(NOT NINJA)
    message(FATAL_ERROR "ninja not found! Please install it via your system package manager!")
endif()

get_filename_component(NINJA_DIR "${NINJA}" DIRECTORY )
vcpkg_add_to_path(PREPEND "${NINJA_DIR}")

message(STATUS "Building TGFX using vendor_tools...")
build_tgfx_with_vendor_tools("${SOURCE_PATH}" "${NODEJS}")

file(INSTALL "${SOURCE_PATH}/include/"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include"
     FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/tgfx-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/tgfx-config.cmake"
    INSTALL_DESTINATION "share/${PORT}"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")

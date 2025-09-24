vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO Tencent/tgfx
        REF 6095b909b1109d4910991a034405f4ae30d6786f
        SHA512 6e6f19b6e87ceb7029912e5c746991904dab5feb6ecc5435f8e05cf94cc4ef16a5e7e181460245aa2c8b5a5c4fff98efc51dcf949ac5fb6cc60109170f428c96
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
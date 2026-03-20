cmake_minimum_required(VERSION 3.13)
project(TGFXWebTest)

set(CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif ()

# Enable dependent modules without setting TGFX_BUILD_TESTS, which would trigger the root
# CMakeLists to create native TGFXFullTest/UpdateBaseline targets that cannot compile for
# Emscripten.
set(TGFX_BUILD_SVG ON)
# TGFX_BUILD_PDF is disabled for now because tgfx has compilation issues on the web platform
# with the AdvancedTypefaceInfo type (WebTypeface.cpp). Enable it once those are fixed.
# set(TGFX_BUILD_PDF ON)
set(TGFX_BUILD_HELLO2D ON)
set(TGFX_USE_FREETYPE ON)
# Web platform only enables WEBP decode by default. Enable encode for baseline image generation.
set(TGFX_USE_WEBP_ENCODE ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# Include vendor.cmake before add_subdirectory so that vendor variables (VENDOR_TOOLS_DIR,
# PLATFORM, ARCH, etc.) are set in this scope. The include_guard(GLOBAL) in vendor.cmake
# prevents it from running again inside the root CMakeLists.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/vendor_tools/vendor.cmake)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ./tgfx)

# Collect test sources (reuse existing tests)
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../test/src/*.*)

# Exclude platform-specific directories
file(GLOB_RECURSE TEST_METAL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../test/src/metal/*.*)
file(GLOB_RECURSE TEST_OPENGL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../test/src/opengl/*.*)
if (TEST_METAL_FILES)
    list(REMOVE_ITEM TEST_SOURCES ${TEST_METAL_FILES})
endif ()
if (TEST_OPENGL_FILES)
    list(REMOVE_ITEM TEST_SOURCES ${TEST_OPENGL_FILES})
endif ()

# Include WebGL-specific test sources
file(GLOB_RECURSE TEST_WEBGL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../test/src/webgl/*.*)
list(APPEND TEST_SOURCES ${TEST_WEBGL_FILES})

# Exclude TestMain.cpp since web uses WebTestRunner.cpp as entry point
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*/TestMain\\.cpp$")

# Exclude PDF tests since TGFX_BUILD_PDF is disabled on web
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*/PDFExportTest\\.cpp$")

# Exclude hidden files (e.g., .DS_Store)
foreach (FILE_PATH ${TEST_SOURCES})
    get_filename_component(FILE_NAME "${FILE_PATH}" NAME_WE)
    if (NOT FILE_NAME)
        list(APPEND TEST_EXCLUDE_FILES ${FILE_PATH})
    endif ()
endforeach ()
if (TEST_EXCLUDE_FILES)
    list(REMOVE_ITEM TEST_SOURCES ${TEST_EXCLUDE_FILES})
endif ()

# Web test adapter sources
file(GLOB WEB_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Build Google Test and harfbuzz
add_vendor_target(test-vendor STATIC_VENDORS harfbuzz googletest CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_vendor_libraries(test-vendor STATIC TEST_VENDOR_LIBS)

add_executable(tgfx-test ${TEST_SOURCES} ${WEB_TEST_SOURCES})
add_dependencies(tgfx-test test-vendor)

# Generate HEAD file with current git commit hash for baseline versioning.
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    OUTPUT_VARIABLE HEAD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/HEAD "${HEAD_COMMIT}")

# Preload web-specific baseline cache if it exists locally (from a previous test run download).
set(CACHE_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/baseline/.cache-web)
set(PRELOAD_CACHE_WEB "")
if (EXISTS ${CACHE_WEB_DIR}/md5.json AND EXISTS ${CACHE_WEB_DIR}/version.json)
    set(PRELOAD_CACHE_WEB
        "SHELL:--preload-file ${CACHE_WEB_DIR}/md5.json@/test/baseline/.cache-web/md5.json"
        "SHELL:--preload-file ${CACHE_WEB_DIR}/version.json@/test/baseline/.cache-web/version.json"
    )
else ()
    message(WARNING "Baseline cache not found at ${CACHE_WEB_DIR}/. "
        "Screenshot tests will pass without actual comparison. "
        "Run 'bash update_baseline.sh' first to generate the baseline cache from main branch.")
endif ()

target_compile_options(tgfx-test PRIVATE
    -fno-rtti
    -fno-access-control
    -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
)

target_compile_definitions(tgfx-test PRIVATE
    GENERATE_BASELINE_IMAGES
    $<$<BOOL:${TGFX_UPDATE_BASELINE}>:UPDATE_BASELINE>
)

target_link_options(tgfx-test PRIVATE
    -lembind
    -fno-rtti
    -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
    -sWASM=1
    -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sMAXIMUM_MEMORY=2GB
    -sEXPORT_NAME='TGFXTest'
    -sEXPORTED_FUNCTIONS=['_RunAllTests','_RunTest']
    -sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','GL']
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sENVIRONMENT=web,worker
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../../resources@/resources"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../../test/baseline/version.json@/test/baseline/version.json"
    "SHELL:--preload-file ${CMAKE_CURRENT_BINARY_DIR}/HEAD@/test/baseline/.cache-web/HEAD"
    ${PRELOAD_CACHE_WEB}
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tgfx-test PRIVATE -g3 -O0)
    target_link_options(tgfx-test PRIVATE
        -g3 -O0
        -gsource-map
        -Wno-limited-postlink-optimizations
    )
else ()
    target_compile_options(tgfx-test PRIVATE -Oz)
    target_link_options(tgfx-test PRIVATE -Oz)
endif ()

set(TGFX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Convert TGFX_INCLUDES from relative paths (relative to tgfx root) to absolute paths.
set(TGFX_ABS_INCLUDES)
foreach (INC ${TGFX_INCLUDES})
    if (IS_ABSOLUTE ${INC})
        list(APPEND TGFX_ABS_INCLUDES ${INC})
    else ()
        list(APPEND TGFX_ABS_INCLUDES ${TGFX_ROOT}/${INC})
    endif ()
endforeach ()

target_include_directories(tgfx-test PRIVATE
    ${TGFX_ROOT}/src
    ${TGFX_ROOT}/test/src
    ${TGFX_ROOT}/hello2d/include
    ${TGFX_ROOT}/third_party/json/include
    ${TGFX_ROOT}/third_party/harfbuzz/src
    ${TGFX_ROOT}/third_party/googletest/googletest
    ${TGFX_ROOT}/third_party/googletest/googletest/include
    ${TGFX_ABS_INCLUDES}
)

target_link_libraries(tgfx-test
    tgfx-hello2d
    tgfx
    ${TEST_VENDOR_LIBS}
)

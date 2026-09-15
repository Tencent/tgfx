# Three-way diff driver consumer (audit F02 / tasks 5-6). Builds DiffDriver against any tgfx
# checkout passed via TGFX_ROOT: the merge-base baseline tree (no AOT code), or the branch.
# The identical driver source must be used on every side; compare_scenes.py rejects output
# directories whose manifests disagree.
cmake_minimum_required(VERSION 3.20)
project(TGFXDiffDriver LANGUAGES C CXX)

if (NOT DEFINED TGFX_ROOT)
    message(FATAL_ERROR "TGFX_ROOT must point at a tgfx checkout (the baseline tree or the branch)")
endif ()
if (NOT EXISTS "${TGFX_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "TGFX_ROOT does not contain a CMakeLists.txt: ${TGFX_ROOT}")
endif ()

# The language standard is set here, before add_subdirectory, so it also covers this consumer
# scope: the tgfx root sets its own standard inside its (child) scope, which does not propagate
# back out to DiffDriver.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TGFX_BUILD_TESTS OFF)
add_subdirectory(${TGFX_ROOT} ${CMAKE_BINARY_DIR}/tgfx-build)

add_executable(DiffDriver ThreeWayDiffDriver.cpp)
target_include_directories(DiffDriver PRIVATE ${TGFX_ROOT}/include)
target_link_libraries(DiffDriver PRIVATE tgfx)
if (APPLE)
    target_link_libraries(DiffDriver PRIVATE "-framework OpenGL")
endif ()

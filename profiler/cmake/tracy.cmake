set(TRACY_PATH ../third_party/tracy)

include(${TRACY_PATH}/cmake/config.cmake)
include(${TRACY_PATH}/cmake/vendor.cmake)

option(NO_FILESELECTOR "Disable the file selector" OFF)
option(GTK_FILESELECTOR "Use the GTK file selector on Linux instead of the xdg-portal one" OFF)
option(LEGACY "Instead of Wayland, use the legacy X11 backend on Linux" OFF)
option(NO_ISA_EXTENSIONS "Disable ISA extensions (don't pass -march=native or -mcpu=native to the compiler)" OFF)
option(NO_STATISTICS "Disable calculation of statistics" OFF)
option(SELF_PROFILE "Enable self-profiling" OFF)


if(SELF_PROFILE)
    add_definitions(-DTRACY_ENABLE)
    add_compile_options(-g -O3 -fno-omit-frame-pointer)
endif()

set(SERVER_FILES
    TracyColor.cpp
    TracyEventDebug.cpp
    TracyFileselector.cpp
    TracyMicroArchitecture.cpp
    TracyProtoHistory.cpp
    TracyStorage.cpp
    TracyWeb.cpp
)


list(TRANSFORM SERVER_FILES PREPEND "${TRACY_PATH}/profiler/src/profiler/")
list(APPEND TRACY_INCLUDE "${TRACY_PATH}/profiler/" ${TRACY_PATH})

set(PROFILER_FILES
        src/ConnectionHistory.cpp
        src/HttpRequest.cpp
        src/ini.c
        src/IsElevated.cpp
        src/ResolvService.cpp
        src/RunQueue.cpp
        src/WindowPosition.cpp
        src/winmain.cpp
        src/winmainArchDiscovery.cpp
)
list(TRANSFORM PROFILER_FILES PREPEND "${TRACY_PATH}/profiler/")


if(NOT EMSCRIPTEN)
    list(APPEND TRACY_LIBS TracyNfd)
endif()
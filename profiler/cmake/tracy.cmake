set(TRACY_PATH ../third_party/tracy)

include(${TRACY_PATH}/cmake/config.cmake)
include(${TRACY_PATH}/cmake/vendor.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/server.cmake)

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
    TracyTexture.cpp
    TracyUserData.cpp
    TracyUtility.cpp
    TracyWeb.cpp
)


list(TRANSFORM SERVER_FILES PREPEND "${TRACY_PATH}/profiler/src/profiler/")

set(PROFILER_FILES
        src/ConnectionHistory.cpp
        src/Filters.cpp
        src/Fonts.cpp
        src/HttpRequest.cpp
        src/ImGuiContext.cpp
        src/ini.c
        src/IsElevated.cpp
        src/ResolvService.cpp
        src/RunQueue.cpp
        src/WindowPosition.cpp
        src/winmain.cpp
        src/winmainArchDiscovery.cpp
)

list(TRANSFORM PROFILER_FILES PREPEND "${TRACY_PATH}/profiler/")

set(INCLUDES "${TRACY_PATH}/profiler")
include_directories(${INCLUDES})

list(APPEND TRACY_LIBS
    TracyServer
    TracyImGui)


if(NOT EMSCRIPTEN)
    list(APPEND TRACY_LIBS TracyNfd)
endif()

list(APPEND TRACY_OPTION)
if (EMSCRIPTEN)
    list(APPEND TRACY_OPTION -pthread -sASSERTIONS=0 -sINITIAL_MEMORY=384mb -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4gb -sSTACK_SIZE=1048576 -sWASM_BIGINT=1 -sPTHREAD_POOL_SIZE=8 -sEXPORTED_FUNCTIONS=_main,_nativeOpenFile -sEXPORTED_RUNTIME_METHODS=ccall -sENVIRONMENT=web,worker --preload-file embed.tracy)

    ile(DOWNLOAD https://share.nereid.pl/i/embed.tracy ${TRACY_PATH}/profiler/embed.tracy EXPECTED_MD5 ca0fa4f01e7b8ca5581daa16b16c768d)
    file(COPY ${TRACY_PATH}/profiler/wasm/index.html DESTINATION ${TRACY_PATH}/profiler)
    file(COPY ${TRACY_PATH}/profiler/wasm/httpd.py DESTINATION ${TRACY_PATH}/profiler)
    file(COPY_FILE ${TRACY_PATH}/profiler/../icon/icon.svg ${TRACY_PATH}/profiler/favicon.svg)
endif()
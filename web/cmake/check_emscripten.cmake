# Verify Emscripten environment for web builds. Homebrew's emscripten package differs from emsdk
# in toolchain layout, sysroot paths, and default compiler flags (e.g. Wasm EH enabled in 5.x),
# causing build failures and runtime crashes. Always use emsdk with `source <path-to-emsdk>/emsdk_env.sh`.
if (EMSCRIPTEN)
    if (NOT DEFINED ENV{EMSDK})
        message(FATAL_ERROR
            "EMSDK environment variable not set. "
            "You appear to be using a system-installed Emscripten (e.g. from Homebrew) "
            "which is not compatible with this project.\n"
            "Please install and activate emsdk 4.0.15:\n"
            "  git clone https://github.com/emscripten-core/emsdk.git\n"
            "  cd emsdk && ./emsdk install 4.0.15 && ./emsdk activate 4.0.15\n"
            "  source ./emsdk_env.sh\n"
            "See README.md for details.")
    elseif (EMSCRIPTEN_VERSION VERSION_GREATER_EQUAL "5.0.0")
        message(FATAL_ERROR
            "Emscripten ${EMSCRIPTEN_VERSION} is not supported (5.x removed -sUSE_WEBGPU). "
            "Please use emsdk 4.0.15:\n"
            "  cd <path-to-emsdk> && ./emsdk install 4.0.15 && ./emsdk activate 4.0.15\n"
            "  source ./emsdk_env.sh")
    endif ()
endif ()

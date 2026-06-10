# Verify Emscripten environment for web builds. Homebrew's emscripten package differs from emsdk
# in toolchain layout, sysroot paths, and default compiler flags (e.g. Wasm EH enabled in 5.x),
# causing build failures and runtime crashes. Always use emsdk with `source <path-to-emsdk>/emsdk_env.sh`.
#
# Version constraint: -sUSE_WEBGPU was removed in 4.0.18 in favor of --use-port=emdawnwebgpu.
# Use emsdk 4.0.15 (the last version with working -sUSE_WEBGPU) until the project migrates.
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
    elseif (EMSCRIPTEN_VERSION VERSION_LESS "4.0.15")
        message(FATAL_ERROR
            "Emscripten ${EMSCRIPTEN_VERSION} is too old. "
            "This project requires emsdk >= 4.0.15 for WebGPU support.\n"
            "  cd <path-to-emsdk> && ./emsdk install 4.0.15 && ./emsdk activate 4.0.15\n"
            "  source ./emsdk_env.sh")
    elseif (EMSCRIPTEN_VERSION VERSION_GREATER_EQUAL "4.0.18")
        message(FATAL_ERROR
            "Emscripten ${EMSCRIPTEN_VERSION} is not supported. "
            "Emscripten >= 4.0.18 removed -sUSE_WEBGPU in favor of --use-port=emdawnwebgpu. "
            "Please use emsdk 4.0.15:\n"
            "  cd <path-to-emsdk> && ./emsdk install 4.0.15 && ./emsdk activate 4.0.15\n"
            "  source ./emsdk_env.sh\n"
            "To use newer Emscripten, migrate WebGPU integration to --use-port=emdawnwebgpu first.")
    endif ()
endif ()

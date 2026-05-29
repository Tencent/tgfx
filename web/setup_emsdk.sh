#!/bin/bash
# Auto-detect and activate emsdk for current shell session.
# Usage: source setup_emsdk.sh   OR   bash setup_emsdk.sh <command...>
#
# When sourced: sets up emsdk environment variables in the current shell.
# When executed with arguments: activates emsdk and runs the given command.
#
# Version constraint: -sUSE_WEBGPU was removed in 4.0.18 in favor of --use-port=emdawnwebgpu.
# Use emsdk 4.0.15 (the last version with working -sUSE_WEBGPU) until the project migrates.

EMSDK_REQUIRED_VERSION="4.0.15"

if [ -z "$EMSDK" ]; then
    # Search common emsdk install locations
    for dir in "$HOME/emsdk" "/opt/emsdk" "$HOME/.emsdk"; do
        if [ -f "$dir/emsdk_env.sh" ]; then
            source "$dir/emsdk_env.sh" >/dev/null 2>&1
            break
        fi
    done
fi

if [ -z "$EMSDK" ]; then
    echo "Error: Could not find emsdk. Please install emsdk ${EMSDK_REQUIRED_VERSION}:" >&2
    echo "  git clone https://github.com/emscripten-core/emsdk.git" >&2
    echo "  cd emsdk && ./emsdk install ${EMSDK_REQUIRED_VERSION} && ./emsdk activate ${EMSDK_REQUIRED_VERSION}" >&2
    echo "Or set EMSDK environment variable to your emsdk directory." >&2
    exit 1
fi

# Check active Emscripten version and auto-switch if incompatible
if command -v emcc >/dev/null 2>&1; then
    EMCC_VERSION=$(emcc --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    if [ "$EMCC_VERSION" != "$EMSDK_REQUIRED_VERSION" ]; then
        echo "Active Emscripten is ${EMCC_VERSION}, switching to ${EMSDK_REQUIRED_VERSION}..."
        (
            cd "$EMSDK"
            ./emsdk install "$EMSDK_REQUIRED_VERSION"
            ./emsdk activate "$EMSDK_REQUIRED_VERSION"
        )
        source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
        echo "Switched to Emscripten $(emcc --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
    fi
fi

# If called with arguments, execute them (used as command wrapper)
if [ $# -gt 0 ]; then
    exec "$@"
fi

#!/bin/bash
# Auto-detect and activate emsdk for current shell session.
# Usage: source setup_emsdk.sh   OR   bash setup_emsdk.sh <command...>
#
# When sourced: sets up emsdk environment variables in the current shell.
# When executed with arguments: activates emsdk and runs the given command.

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
    echo "Error: Could not find emsdk. Please install emsdk 4.0.15:" >&2
    echo "  git clone https://github.com/emscripten-core/emsdk.git" >&2
    echo "  cd emsdk && ./emsdk install 4.0.15 && ./emsdk activate 4.0.15" >&2
    echo "Or set EMSDK environment variable to your emsdk directory." >&2
    exit 1
fi

# If called with arguments, execute them (used as command wrapper)
if [ $# -gt 0 ]; then
    exec "$@"
fi

#!/bin/bash
cd $(dirname $0)

./install_tools.sh

if [[ `uname` == 'Darwin' ]]; then
  if [ ! $(which emcc) ]; then
      echo "emscripten not found. Trying to install..."
      brew install emscripten
  fi
fi

if [ ! $(which depsync) ]; then
  echo "depsync not found. Trying to install..."
  npm install -g depsync > /dev/null
else
  npm update -g depsync > /dev/null
fi

depsync || exit 1

# Sync shaderc's sub-dependencies (SPIRV-Tools, glslang, etc.) on Apple platforms only
if [[ `uname` == 'Darwin' ]]; then
  if [ -f "third_party/shaderc/utils/git-sync-deps" ]; then
    python3 third_party/shaderc/utils/git-sync-deps || exit 1
  fi
fi

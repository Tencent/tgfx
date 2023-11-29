#!/bin/bash
cd $(dirname $0)

./install_tools.sh

if [[ `uname` == 'Darwin' ]]; then
  if [ ! $(emcc --version) ]; then
      echo "emscripten not found. Trying to install..."
      ./web/script/install-emscripten.sh
  fi
fi

if [ ! $(depsync -v) ]; then
  echo "depsync not found. Trying to install..."
  npm install -g depsync
else
  npm update -g depsync --silent
fi

depsync || exit 1
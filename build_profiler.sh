#!/bin/bash

if ! [[ -d "third_party/tracy" ]]; then
  ./sync_deps.sh
fi

profiler_dir=third_party/tracy/profiler

if [ ! $(which $profiler_dir/build/tracy-profiler) ]; then
    mkdir -p $profiler_dir/build
    cd $profiler_dir/build
    cmake ..
    make
fi

$profiler_dir/build/tracy-profiler || exit 1

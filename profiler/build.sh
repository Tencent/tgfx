#!/bin/bash

build_dir=$(dirname "$0")/build
profiler_dir=$(dirname "$0")

if ! [[ -d "$(dirname "$0")/../third_party/tracy" ]]; then
  sh $(dirname "$0")/../sync_deps.sh
fi

if [ ! $(which $build_dir/Profiler) ]; then
    mkdir $build_dir
    cmake -B $build_dir -S $profiler_dir -DCMAKE_BUILD_TYPE=Release
    make -C $build_dir
fi

$build_dir/Profiler || exit 1

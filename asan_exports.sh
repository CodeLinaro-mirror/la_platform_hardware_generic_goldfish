#!/bin/bash
# This script sets the ASAN configuration needed
# to run the android emulator

# Magic to figure out if we have been source
if [ "${BASH_SOURCE-}" = "$0" ]; then
  echo "You must source this script: \$ source $0" >&2
  exit 33
fi

sourced_file_path="${BASH_SOURCE[0]:-${(%):-%x}}"
sourced_dir=$(dirname $sourced_file_path)
script_dir=$(realpath "$sourced_dir")

suppressions=$(realpath $script_dir/leak_suppressions.txt)

if [[ "$(uname -s)" == "Darwin" ]]; then
   symbolizer=$(realpath $script_dir/../../../prebuilts/clang/host/darwin-x86/clang-r536225/bin/llvm-symbolizer)
else
   symbolizer=$(realpath $script_dir/../../../prebuilts/clang/host/linux-x86/clang-r536225/bin/llvm-symbolizer)
fi

export ASAN_OPTIONS=detect_odr_violation=0
export ASAN_SYMBOLIZER_PATH=$symbolizer
export LSAN_OPTIONS=suppressions=$suppressions

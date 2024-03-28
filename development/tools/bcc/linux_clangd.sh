#!/bin/bash
# Copyright 2024 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Resolve clangd and clang compiler toolchains for AOSP compiler toolchains
# You can use this launcher if you need a clangd server.


# If you are using the vs-code clangd plugin for example you would set the
# "clangd.path": "<path-to-this-script>"
# property


# Calculate AOSP_ROOT using relative paths, assuming consistent structure
AOSP_ROOT=$(CDPATH= cd -- "$(dirname $0)/../../../../../.." && pwd)

# Extract Clang version from JSON (assumes the 'clang' field is always present)
CLANG_VER=$(jq -r '.clang' "$AOSP_ROOT/build/bazel/rules/toolchains.json")
CLANG_VER="${CLANG_VER%\"}"  # Remove trailing quotation mark
CLANG_VER="${CLANG_VER#\"}"  # Remove leading quotation mark

# Construct prebuilts path
PREBUILTS_PATH="$AOSP_ROOT/prebuilts/clang/host/linux-x86/$CLANG_VER/bin"

# echo "Using compiler toolchains from: $PREBUILTS_PATH"
# Add prebuilts path to PATH, forcing all compilers to be found in $PREBUILTS_PATH
export PATH="$PREBUILTS_PATH"

# Execute clangd
clangd $@

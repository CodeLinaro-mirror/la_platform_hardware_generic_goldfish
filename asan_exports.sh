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

AOSP_ROOT=$(realpath $script_dir/../../../)

TOOL_VERSIONS_JSON="$AOSP_ROOT/build/bazel/toolchains/tool_versions.json"

# Attempt to extract the clang version, jq will parse everything..
if command -v jq &>/dev/null; then
    CLANG_VER=$(jq -r '.clang' "$TOOL_VERSIONS_JSON")
else
    # Fallback to sed if jq is not found.
    # This sed command extracts the 'clang' version.
    #
    # Note: This relies on the "clang" key and its value being
    # entirely on a single line within the JSON file. If the JSON were formatted
    # with line breaks within the value string (e.g., "clang": "\nversion\n"),
    # or if the key-value pair itself spanned multiple lines, this sed regex
    # would likely fail to extract the correct information.
    #
    # - `-n`: Suppresses default line printing.
    # - `s/.../.../p`: Substitute command.
    #   - `.*"clang": *"`: Matches everything from the start of the line up to
    #     `"clang": `, including any spaces.
    #   - `\([^"]*\)`: **Captures** any characters that are *not* a double quote (`"`).
    #     This is our target version string (e.g., "clang-r530567").
    #   - `".*`: Matches the closing quote and the rest of the line.
    #   - `\1`: Replaces the matched line with only the captured version string.
    #   - `p`: Prints the line only if the substitution was successful.
    CLANG_VER=$(sed -n 's/.*"clang": *"\([^"]*\)".*/\1/p' "$TOOL_VERSIONS_JSON")
fi

# jq returns the string "null" if the key is not found; sed returns an empty string.
if [[ -z "$CLANG_VER" || "$CLANG_VER" == "null" ]]; then
    echo "Error: Could not extract 'clang' version from $TOOL_VERSIONS_JSON" >&2
    echo "Please ensure the file exists and contains a 'clang' key with a valid version." >&2
    return 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    symbolizer=$(realpath $AOSP_ROOT/prebuilts/clang/host/darwin-x86/${CLANG_VER}/bin/llvm-symbolizer)
else
    symbolizer=$(realpath  $AOSP_ROOT/prebuilts/clang/host/linux-x86/${CLANG_VER}/bin/llvm-symbolizer)
fi

export ASAN_OPTIONS=detect_odr_violation=0
export ASAN_SYMBOLIZER_PATH=$symbolizer
export LSAN_OPTIONS=suppressions=$suppressions

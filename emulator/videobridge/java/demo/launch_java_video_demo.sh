#!/bin/bash
# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Fail on error
set -e

# Find the directory of this script (supports both Bash and Zsh)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")" && pwd)"
FIND_ROOT="$SCRIPT_DIR"
while [ "$FIND_ROOT" != "/" ] && [ ! -f "$FIND_ROOT/WORKSPACE" ]; do
    FIND_ROOT="$(dirname "$FIND_ROOT")"
done
WORKSPACE_ROOT="$FIND_ROOT"

# Parse arguments
DISCOVERY_FILE=""
USE_COMPOSE=true

usage() {
    echo "Usage: $0 --discovery_file <path_to_ini> [--swing|--compose]"
    echo ""
    echo "Options:"
    echo "  --discovery_file Path to active emulator PID discovery file (e.g. ~/Library/Caches/TemporaryItems/avd/running/pid_XXXXX.ini)"
    echo "  --swing          Launch the Swing Kotlin WebRTC demo (default: Compose Desktop)"
    echo "  --compose        Launch the Compose Desktop Kotlin WebRTC demo"
    echo "  --help|-h        Show this help message"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --discovery_file)
            DISCOVERY_FILE="$2"
            shift 2
            ;;
        --swing)
            USE_COMPOSE=false
            shift
            ;;
        --compose)
            USE_COMPOSE=true
            shift
            ;;
        --help|-h)
            usage
            ;;
        *)
            echo "Unknown argument: $1"
            usage
            ;;
    esac
done

if [ -z "$DISCOVERY_FILE" ]; then
    echo "Error: --discovery_file is required."
    echo "Usage: $0 --discovery_file <path_to_ini> [--swing|--compose]"
    exit 1
fi

# Resolve absolute path of discovery file before changing directories
DISCOVERY_FILE_ABS="$(cd "$(dirname "$DISCOVERY_FILE")" && pwd)/$(basename "$DISCOVERY_FILE")"

if [ ! -f "$DISCOVERY_FILE_ABS" ]; then
    echo "Error: Discovery file not found at: $DISCOVERY_FILE"
    exit 1
fi

# Parse gRPC port from discovery file
EMULATOR_PORT=$(grep -i '^grpc.port=' "$DISCOVERY_FILE_ABS" | cut -d'=' -f2 | tr -d '\r' | tr -d ' ')

if [ -z "$EMULATOR_PORT" ]; then
    echo "Error: Unable to parse grpc.port from discovery file: $DISCOVERY_FILE_ABS"
    exit 1
fi

echo "Found emulator gRPC service on port $EMULATOR_PORT from discovery file."

cd "$WORKSPACE_ROOT"

if [ "$USE_COMPOSE" = true ]; then
    DEMO_NAME="webrtc_compose_demo"
    DEMO_TYPE_LABEL="Kotlin Compose Desktop WebRTC"
else
    DEMO_NAME="webrtc_demo"
    DEMO_TYPE_LABEL="Kotlin Swing WebRTC"
fi

DEMO_TARGET="@goldfish//emulator/videobridge/java/demo:$DEMO_NAME"
DEMO_BIN="$WORKSPACE_ROOT/bazel-bin/external/goldfish+/emulator/videobridge/java/demo/$DEMO_NAME"

# 1. Build Demo binary
echo "Building $DEMO_TYPE_LABEL Demo binary..."
bazel build "$DEMO_TARGET"

# 2. Wait for Emulator gRPC port to be active
echo "Waiting for emulator gRPC service to start listening on port $EMULATOR_PORT..."
while ! nc -z localhost "$EMULATOR_PORT" 2>/dev/null; do
    sleep 0.5
done
echo "Emulator gRPC service (with built-in WebRTC videobridge) is online!"

# 3. Launch WebRTC Client Demo connected directly to emulator gRPC service
echo "Launching $DEMO_TYPE_LABEL Demo..."
echo ""
echo "--------------------------------------------------------"
echo "$DEMO_TYPE_LABEL Client connects to emulator gRPC service at localhost:$EMULATOR_PORT"
echo "--------------------------------------------------------"
echo ""

"$DEMO_BIN" localhost "$EMULATOR_PORT"


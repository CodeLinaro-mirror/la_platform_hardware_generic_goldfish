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

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIND_ROOT="$SCRIPT_DIR"
while [ "$FIND_ROOT" != "/" ] && [ ! -f "$FIND_ROOT/WORKSPACE" ]; do
    FIND_ROOT="$(dirname "$FIND_ROOT")"
done
WORKSPACE_ROOT="$FIND_ROOT"
cd "$WORKSPACE_ROOT"

PORT_BRIDGE="8554"
DISCOVERY_FILE=""

usage() {
    echo "Usage: $0 [--discovery_file <path_to_pid_XXXXX.ini>] [--port <grpc_port>] [--swing]"
    echo ""
    echo "Options:"
    echo "  --discovery_file Path to active emulator PID discovery file (e.g. ~/Library/Caches/TemporaryItems/avd/running/pid_XXXXX.ini)"
    echo "  --port           gRPC port for Video Bridge signaling server (default: 8554)"
    echo "  --swing          Launch the Swing Kotlin WebRTC demo (default: Compose Desktop)"
    echo "  --compose        Launch the Compose Desktop Kotlin WebRTC demo"
    echo "  --help           Show this help message"
    exit 1
}

USE_COMPOSE=true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --discovery_file)
            DISCOVERY_FILE="$2"
            shift 2
            ;;
        --port)
            PORT_BRIDGE="$2"
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

# If no discovery file specified, auto-discover newest pid_*.ini file
discovery_dir=""
if [ -z "$DISCOVERY_FILE" ]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        discovery_dir="$HOME/Library/Caches/TemporaryItems/avd/running"
    elif [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* ]]; then
        discovery_dir="$LOCALAPPDATA/Temp/avd/running"
    else
        discovery_dir="${TMPDIR:-/tmp}/avd/running"
    fi

    if [ -d "$discovery_dir" ]; then
        DISCOVERY_FILE="$(ls -t "$discovery_dir"/pid_*.ini 2>/dev/null | head -n 1 || true)"
    fi
fi

DISCOVERY_ARGS=""
if [ -n "$DISCOVERY_FILE" ] && [ -f "$DISCOVERY_FILE" ]; then
    echo "Discovered emulator discovery file: $DISCOVERY_FILE"
    DISCOVERY_ARGS="--discovery_file $DISCOVERY_FILE"
else
    echo "Warning: No active emulator discovery file found. Video Bridge will attempt default port 5554."
fi

# Clear existing videobridge process on PORT_BRIDGE if present
existing_pids="$(lsof -ti:"$PORT_BRIDGE" 2>/dev/null || true)"
if [ -n "$existing_pids" ]; then
    for pid in $existing_pids; do
        cmdline="$(ps -p "$pid" -o command= 2>/dev/null || true)"
        if [[ "$cmdline" == *"videobridge"* ]]; then
            echo "Terminating existing videobridge process (PID $pid)..."
            kill -15 "$pid" 2>/dev/null || true
            sleep 0.5
        fi
    done
fi

if [ "$USE_COMPOSE" = true ]; then
    DEMO_NAME="webrtc_compose_demo"
    DEMO_TYPE_LABEL="Kotlin Compose Desktop WebRTC"
else
    DEMO_NAME="webrtc_demo"
    DEMO_TYPE_LABEL="Kotlin Swing WebRTC"
fi

DEMO_TARGET="@goldfish//emulator/videobridge/java/demo:$DEMO_NAME"
DEMO_BIN="$WORKSPACE_ROOT/bazel-bin/external/goldfish+/emulator/videobridge/java/demo/$DEMO_NAME"

echo "Building Video Bridge and $DEMO_TYPE_LABEL Demo binaries..."
bazel build @goldfish//emulator/videobridge:videobridge "$DEMO_TARGET"

# Cleanup handler for Video Bridge background process
PID_BRIDGE=""
cleanup() {
    if [ -n "$PID_BRIDGE" ] && kill -0 "$PID_BRIDGE" 2>/dev/null; then
        echo "Shutting down Video Bridge (PID $PID_BRIDGE)..."
        kill "$PID_BRIDGE" 2>/dev/null || true
        wait "$PID_BRIDGE" 2>/dev/null || true
        echo "Done."
    fi
}
trap cleanup EXIT INT TERM

# Start Video Bridge binary in background
echo "Launching Video Bridge on port $PORT_BRIDGE..."
"$WORKSPACE_ROOT/bazel-bin/external/goldfish+/emulator/videobridge/videobridge" $DISCOVERY_ARGS --grpc_address "localhost:$PORT_BRIDGE" &
PID_BRIDGE=$!

# Wait for Video Bridge to start listening on gRPC port
echo "Waiting for Video Bridge to start listening on port $PORT_BRIDGE..."
while ! nc -z localhost "$PORT_BRIDGE" 2>/dev/null; do
    sleep 0.2
done
sleep 1
echo "Video Bridge is online!"

# Launch WebRTC Client Demo
echo "Launching $DEMO_TYPE_LABEL Demo..."
echo ""
echo "--------------------------------------------------------"
echo "$DEMO_TYPE_LABEL Client connects to Videobridge at localhost:$PORT_BRIDGE"
echo "--------------------------------------------------------"
echo ""

"$DEMO_BIN" localhost "$PORT_BRIDGE"

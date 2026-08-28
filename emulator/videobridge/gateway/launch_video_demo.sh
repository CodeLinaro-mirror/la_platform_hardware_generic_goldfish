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
DIR="$( cd "$( dirname "${BASH_SOURCE[0]:-${(%):-%x}}" )" && pwd )"
cd "$DIR"

# Parse arguments
DISCOVERY_FILE=""
PORT_GATEWAY=8080

while [[ $# -gt 0 ]]; do
    case $1 in
        --discovery_file)
            DISCOVERY_FILE="$2"
            shift 2
            ;;
        --port)
            PORT_GATEWAY="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: ./launch_video_demo.sh --discovery_file <path_to_ini> [--port <gateway_port>]"
            exit 1
            ;;
    esac
done

if [ -z "$DISCOVERY_FILE" ]; then
    echo "Error: --discovery_file is required."
    echo "Usage: ./launch_video_demo.sh --discovery_file <path_to_ini> [--port <gateway_port>]"
    exit 1
fi

# Resolve absolute path of discovery file before changing directories
DISCOVERY_FILE_ABS="$(cd "$(dirname "$DISCOVERY_FILE")" && pwd)/$(basename "$DISCOVERY_FILE")"

if [ ! -f "$DISCOVERY_FILE_ABS" ]; then
    echo "Error: Discovery file not found at: $DISCOVERY_FILE"
    exit 1
fi

# Parse gRPC port and token from discovery file
EMULATOR_PORT=$(grep -i '^grpc.port=' "$DISCOVERY_FILE_ABS" | cut -d'=' -f2 | tr -d '\r' | tr -d ' ')
EMULATOR_TOKEN=$(grep -i '^grpc.token=' "$DISCOVERY_FILE_ABS" | cut -d'=' -f2 | tr -d '\r' | tr -d ' ')

if [ -z "$EMULATOR_PORT" ]; then
    echo "Error: Unable to parse grpc.port from discovery file: $DISCOVERY_FILE_ABS"
    exit 1
fi

echo "Found emulator gRPC service on port $EMULATOR_PORT from discovery file."

# 1. Setup / Activate Python environment
if [ ! -d "venv" ]; then
    echo "Python virtual environment not found. Running setup_env.sh..."
    ./setup_env.sh
fi

echo "Activating virtual environment..."
source venv/bin/activate

# 2. Wait for Emulator gRPC port to be active
echo "Waiting for emulator gRPC service to start listening on port $EMULATOR_PORT..."
while ! nc -z localhost "$EMULATOR_PORT" 2>/dev/null; do
    sleep 0.5
done
echo "Emulator gRPC service (with built-in WebRTC videobridge) is online!"

# 3. Launch Python Web Gateway connected directly to emulator's built-in WebRTC videobridge
echo "Launching Python Signaling Gateway on port $PORT_GATEWAY..."
echo ""
echo "--------------------------------------------------------"
echo "WebRTC stream is ready! Open the following URL in your browser:"
echo "    https://pokowaka.github.io/android-emulator-webrtc/?url=localhost:$PORT_GATEWAY"
echo "--------------------------------------------------------"
echo ""

GATEWAY_ARGS=(
    "--port=$PORT_GATEWAY"
    "--videobridge=localhost:$EMULATOR_PORT"
    "--discovery_file=$DISCOVERY_FILE_ABS"
)

if [ -n "$EMULATOR_TOKEN" ]; then
    GATEWAY_ARGS+=("--videobridge_token=$EMULATOR_TOKEN")
fi

videobridge-gateway "${GATEWAY_ARGS[@]}"

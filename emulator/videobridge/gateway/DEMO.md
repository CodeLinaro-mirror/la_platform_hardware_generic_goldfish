# End-to-End Demo Guide: Emulator WebRTC Streaming

This guide explains how to set up and run a complete, end-to-end streaming demo
using the **Android Emulator (with built-in WebRTC Video Bridge)**, the **Python Gateway Server**, and the
React frontend example application from the
[android-emulator-webrtc](https://github.com/pokowaka/android-emulator-webrtc)
repository.

---

## E2E Architecture Flow

```
 +-----------------+                +-----------------+                +-----------------+
 |   React Web     |    HTTP REST   |  Python Gateway |   gRPC (TCP)   | Android Emulator|
 |  Frontend App   |--------------->|     Server      |--------------->| (with built-in  |
 | (localhost:3000)|  WS Signaling  | (localhost:8080)|  gRPC Signaling|  WebRTC Bridge) |
 |                 |===============>|                 |===============>| (grpc.port)     |
 |                 |                +-----------------+                +-----------------+
 |                 |                                                            |
 |                 |                     WebRTC Data & Media Streams            |
 |                 |<===========================================================|
 +-----------------+                         (UDP / SRTP)
```

---

## Step 1: Launch your Android Emulator

Ensure you have a running emulator with the gRPC service enabled. The WebRTC Video
Bridge is now integrated directly into the emulator / QEMU binary.

1. Locate the active emulator's **discovery configuration file** (ending in
   `.ini`).
   - **macOS default location**: `~/Library/Caches/TemporaryItems/avd/running/pid_<PID>.ini`
   - **Linux default location**: `~/.android/avd/running/pid_<PID>.ini` or `${TMPDIR}/avd/running/pid_<PID>.ini`
   - _Note: The file contains configuration properties like `grpc.port=<port>`
     and `grpc.token=<token>`._

---

## Quick Start: Launch Everything with One Script

If you want to start the Python Signaling Gateway connected to your emulator
with a single command, you can use the provided
[launch_video_demo.sh](./launch_video_demo.sh) script:

1. Navigate to the gateway directory:
   ```bash
   cd hardware/generic/goldfish/emulator/videobridge/gateway
   ```
2. Run the script, providing the path to the active emulator discovery `.ini`
   file:
   ```bash
   ./launch_video_demo.sh --discovery_file /path/to/pid_<PID>.ini
   ```
   _Note: This script will parse the gRPC port and token from the discovery file,
   set up the Python virtual environment if missing, wait for the emulator's gRPC
   service, and launch the Python Gateway._
3. Once the signaling gateway starts, open the following URL in your browser to
   view the WebRTC stream immediately:
   [https://pokowaka.github.io/android-emulator-webrtc/?url=localhost:8080](https://pokowaka.github.io/android-emulator-webrtc/?url=localhost:8080)
4. When you are done, press **Ctrl+C** to stop the process.

---

## Step 2: Built-in Emulator WebRTC Video Bridge

The WebRTC Video Bridge is built directly into QEMU / Android Emulator. It handles
high-performance media encoding and forwards user input events from data channels
back to the emulator OS. A separate standalone `videobridge` binary is no longer
required.

The emulator's built-in gRPC service listens on the `grpc.port` specified in the PID
discovery `.ini` file.

---

## Step 3: Start the Python Gateway Server

The Python Gateway translates HTTP REST and WebSocket JSEP signaling into the
emulator's native `rtc2` gRPC signaling surface.

The gateway is set up as a standard Python project using `pyproject.toml`
containing all dependencies.

### Option A: Install and Run using setup_env.sh (Recommended)

1. Open a terminal window and navigate to the gateway directory:
   ```bash
   cd hardware/generic/goldfish/emulator/videobridge/gateway
   ```
2. Set the `$BAZEL_ROOT` environment variable to the root of the `emu-main-next`
   workspace (if not already set, e.g. via `mise`):
   ```bash
   export BAZEL_ROOT=/path/to/emu-main-next
   ```
3. Run the environment setup script to automatically create a virtual
   environment, compile the proto definitions, and install dependencies:
   ```bash
   ./setup_env.sh
   ```
4. Activate the newly created virtual environment:
   ```bash
   source venv/bin/activate
   ```
5. Run the gateway server, passing `--videobridge` pointing to the emulator's gRPC port:
   ```bash
   videobridge-gateway \
     --port=8080 \
     --videobridge=localhost:<EMULATOR_GRPC_PORT> \
     --discovery_file=/path/to/pid_<PID>.ini
   ```

### Option B: Run Directly from Source

If you already have `aiohttp`, `grpcio`, and `websockets` installed in your
python environment:

1. Run the script directly from the source directory:
   ```bash
   python3 src/videobridge_gateway/gateway_server.py \
     --port=8080 \
     --videobridge=localhost:<EMULATOR_GRPC_PORT> \
     --discovery_file=/path/to/pid_<PID>.ini
   ```

Verify that the gateway server starts and binds successfully to
http://localhost:8080.

---

## Step 4: Access the WebRTC Frontend

Instead of cloning and running the frontend locally, you can use the hosted demo
application:

1. Open your browser and navigate to:
   https://pokowaka.github.io/android-emulator-webrtc/
2. In the configuration panel, point the **Signaling/Gateway URL** to your local
   gateway server: `http://localhost:8080`
3. Click **Connect To Emulator**

---

## Step 5: Interact with the E2E Demo

Once the application connects:

1. The UI will request the emulator status, fetch the ICE configuration, perform
   JSEP SDP negotiation, and initialize the peer connection.
2. **Result**: You should see the Android screen streaming in real-time in your
   browser!
3. **Interaction**:
   - Click/drag on the screen inside the browser to send touch/mouse inputs.
   - Type on your physical keyboard to send key presses to the emulator.
   - Use the UI inputs to test geolocations (sending mock latitude/longitude
     updates).


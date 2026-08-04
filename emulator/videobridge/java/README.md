# WebRTC Client Java/Kotlin Library (`webrtc_client_java`)

High-performance, UI-agnostic Java and Kotlin SDK for receiving real-time WebRTC
video streams and forwarding multi-touch, mouse, and keyboard input events to
Android instances (Local Emulator, Cloud Instances, and Physical Devices).

______________________________________________________________________

## 1. Developer Onboarding & Getting Started

### Architecture Overview

```
              +---------------------------------------+
              |         UI Layer (Swing/Compose)       |
              +---------------------------------------+
                                  |
                                  v
              +---------------------------------------+
              |         EmulatorStreamSession         |
              +---------------------------------------+
                           /             \
                          v               v
  +-------------------------------+    +----------------------------------+
  | VideoBridgeSignalingClient    |    |          WebRtcClient            |
  | (gRPC / RtcServiceV2)         |    | (Native C++ JNI WebRTC Receiver) |
  +-------------------------------+    +----------------------------------+
                  |                                      |
                  v (gRPC Signaling)                     v (WebRTC Media/Data)
  +-----------------------------------------------------------------------+
  |                         Video Bridge / Peer Engine                    |
  +-----------------------------------------------------------------------+
```

### Key Components

- **`WebRtcClient`**: Low-level wrapper around native C++ WebRTC peer
  connection. Manages JSEP SDP negotiation, HW-accelerated video frame decoding,
  and data channel event serialization.
- **`EmulatorStreamSession`**: High-level stream controller managing session
  lifecycle, converting raw video buffers to `BufferedImage` objects, and
  managing signaling state transitions.
- **`VideoBridgeSignalingClient`**: Default gRPC signaling transport
  communicating with the emulator's `RtcServiceV2` endpoint.

### Bazel Dependency Setup

Add `:webrtc_client_java` to your target's `deps` in `BUILD.bazel`:

```starlark
kt_jvm_library(
    name = "my_webrtc_app",
    srcs = ["MyWebRtcApp.kt"],
    deps = [
        "@goldfish//emulator/videobridge/java:webrtc_client_java",
    ],
    data = [
        "@goldfish//emulator/videobridge/java:libwebrtc_java_receiver",
    ],
)
```

______________________________________________________________________

## 2. Using the `RtcServiceV2` Endpoint

`RtcServiceV2` is the gRPC signaling service exposed by Video Bridge / Android
Emulator.

### Basic Setup Example

```kotlin
import com.android.emulator.webrtc.session.EmulatorStreamSession
import com.android.emulator.webrtc.MouseButton
import com.android.emulator.webrtc.TouchAction

fun main() {
    val session = EmulatorStreamSession(host = "localhost", port = 8554)

    // Receive decoded video frames
    session.addFrameListener { bufferedImage ->
        println("Decoded Frame: ${bufferedImage.width}x${bufferedImage.height}")
    }

    // Monitor connection state
    session.addStateListener { state ->
        println("WebRTC Connection State: $state")
    }

    // Start signaling and WebRTC connection
    session.start()

    // Send touch/mouse events
    session.sendMouseEvent(x = 540, y = 1170, button = MouseButton.LEFT, action = TouchAction.DOWN.ordinal)
    session.sendMouseEvent(x = 540, y = 1170, button = MouseButton.NONE, action = TouchAction.UP.ordinal)

    // Send keyboard events
    session.sendKeyEvent(key = "GoHome", down = true)
    session.sendKeyEvent(key = "GoHome", down = false)
}
```

______________________________________________________________________

## 3. Custom Signaling & Message Channels

While `VideoBridgeSignalingClient` uses gRPC (`RtcServiceV2`), `WebRtcClient` is
**completely decoupled from gRPC signaling**. In remote cloud environments,
peer-to-peer networks, or WebRTC relays behind firewalls, you can substitute a
custom signaling transport (WebSocket, Firebase Realtime Database, MQTT, or
REST/gRPC gateways).

### Custom Signaling Transport Implementation

```kotlin
class CustomWebSocketSignaling(val webSocketUrl: String) {
    private val client = WebRtcClient.create()

    fun connect() {
        // 1. Listen for outbound JSEP messages (SDP Offer / ICE Candidates) from native WebRTC
        client.setJsepMessageListener { jsepJson ->
            sendOverWebSocket(jsepJson)
        }

        // 2. Pass inbound JSEP messages (SDP Answer / ICE Candidates) from remote peer to native WebRTC
        onWebSocketMessageReceived { jsepJson ->
            client.acceptJsepMessage(jsepJson)
        }

        // 3. Initiate SDP offer
        client.createOffer()
    }
}
```

______________________________________________________________________

## 4. Data Channels & Over-The-Wire Protocols

WebRTC connection setup establishes both media tracks (H.264/VP8 video stream)
and WebRTC Data Channels for real-time input.

### WebRTC Data Channels

| Channel Name | Direction | Payload Format | Description |
| :------------------- | :---------------------------- | :------------------------------------------------ | :---------------------------------------------------------------------------------------- |
| `input` / `input-v2` | Client $\\rightarrow$ Peer | Protobuf (`android.emulation.control.InputEvent`) | Binary channel transmitting user touch, mouse, and keyboard interaction events. |
| `control` | Client $\\leftrightarrow$ Peer | JSON | Control channel for bitrate adjustment, frame rate negotiation, and system notifications. |

### Over-The-Wire Payload Specification

#### 1. Signaling Payload (JSEP JSON)

Outbound and inbound signaling messages use standard WebRTC JSEP JSON
structures:

```json
{
  "messageType": "offer",
  "sdp": "v=0\r\no=- 123456789 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0 1\r\n..."
}
```

#### 2. Input Event Payload (Protobuf over Data Channel)

Input events sent over the `input` data channel are serialized binary
`android.emulation.control.InputEvent` protocol buffer messages
(`emulator_controller.proto`).

- **Mouse & Touch Events**:

```protobuf
message InputEvent {
    TouchEvent touch_event = 1;
    MouseEvent mouse_event = 2;
}
```

- **Keyboard Events**:

```protobuf
message InputEvent {
    KeyboardEvent key_event = 3;
}

message KeyboardEvent {
    string key = 1;         // W3C KeyboardEvent.key string (e.g. "a", "Enter", "GoHome", "GoBack")
    KeyEventType eventtype = 2; // keydown = 0, keyup = 1, keypress = 2
}
```

______________________________________________________________________

## 5. Multi-Target Vision: Local Emulator, Cloud Remote, and Physical Devices

This library is designed as a unified WebRTC client layer across three target
deployment models:

```
                +-----------------------+
                |   webrtc_client_java  |
                +-----------------------+
                    /        |        \
                   /         |         \ 
                  v          v          v
        +------------+ +------------+ +------------------+
        |   Local    | |   Remote   | | Physical Devices |
        | Emulator   | | Cloud Emu  | | (Android Phone)  |
        +------------+ +------------+ +------------------+
```

### 1. Local Emulator (Current Architecture)

- **Signaling**: Direct gRPC connection to `VideoBridge` running alongside the
  emulator instance on `localhost`.
- **Media/Data**: Direct local loopback WebRTC connection.

### 2. Remote Cloud Emulators (Cuttlefish / GCP / AWS)

- **Signaling**: WebSocket or gRPC signaling gateway traversing NAT/firewalls.
- **Media/Data**: STUN/TURN ICE candidate resolution via WebRTC.

### 3. Physical Android Devices

To connect `webrtc_client_java` to physical Android devices:

#### What Needs to Be Built for Physical Devices:

1. **On-Device WebRTC Daemon / Agent (Android App or Native Daemon)**:
   - **Video Capturer**: Capture device screen using Android `MediaProjection`
     API or `SurfaceControl` framebuffer and encode via `MediaCodec`
     (H.264/VP8).
   - **Input Injector**: Inject touch/keyboard events received from the `input`
     data channel into Android OS using Android `InputManager`
     (`injectInputEvent`) or ADB `/dev/input/event*` nodes.
   - **WebRTC Native PeerConnection**: Host the native WebRTC peer connection
     on-device.
1. **Signaling Server**: Relay SDP offer/answer between `webrtc_client_java`
   client and the physical Android device.

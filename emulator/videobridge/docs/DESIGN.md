# Modern WebRTC Video Bridge Design

This document details the architecture, design, and internal workings of the
WebRTC Video Bridge in `emu-main-next`.

---

## 1. High-Level Architecture & Evolution

The WebRTC Video Bridge components deliver real-time, high-performance audio/video
streaming and user input data channel handling for Android instances.

### In-Process QEMU Integration

While the WebRTC engine previously operated as a standalone out-of-process proxy binary (`videobridge`), the Video Bridge has been integrated directly into QEMU / Android Emulator.

1. **Streamlined Deployment:** Clients (WebRTC Java SDK, Python Gateway, etc.) connect directly to the emulator's native gRPC service (`RtcServiceV2` / `grpc.port`) without requiring a separate proxy process or secondary port management.
2. **Reduced Latency:** Frames and input events are routed directly within QEMU, avoiding inter-process IPC or gRPC proxy hops.
3. **Flexible Architecture:** The core WebRTC C++ components (`Switchboard`, `Participant`, format pipelines, platform HW codecs) remain modular so they can be built into QEMU or used as standalone components where required.

```mermaid
graph LR
    Browser[Web Browser / Client] <-->|gRPC RtcServiceV2| Emulator[Android Emulator / QEMU]
```

---

## 2. A WebRTC Primer for Newcomers

To understand why the `videobridge` architecture is structured this way, we must
first understand how WebRTC works under the hood.

### 2.1 Peer-to-Peer (P2P) is Hard

WebRTC is designed to establish direct, low-latency, peer-to-peer audio, video,
and data streams between a browser and a remote endpoint (in this case, our
emulator). However, direct communication over the internet is blocked by
firewalls and Network Address Translators (NATs).

To solve this, WebRTC uses three core concepts:

1. **SDP (Session Description Protocol):** A text format describing the media
   capabilities of an endpoint (e.g., "I support VP8 video, Opus audio, and want
   to receive them at these resolutions").
2. **ICE (Interactive Connectivity Establishment):** A framework used to find
   all possible ways two peers can connect (local IP addresses, public IP
   addresses via STUN servers, or relayed connections via TURN servers). Each
   potential connection path is called an **ICE Candidate**.
3. **Signaling:** Before two peers can talk directly, they must exchange their
   SDPs (the "Offer" and the "Answer") and their ICE Candidates. WebRTC does
   _not_ define a signaling protocol. In our architecture, we use the gRPC
   `RtcServiceV2` endpoint directly for this exchange. If a client requires a
   different transport (like WebSockets to connect to a browser), a lightweight
   proxy gateway can be placed in front of the gRPC interface (as demonstrated
   in [DEMO.md](../gateway/DEMO.md)).

### 2.2 JSEP (JavaScript Session Establishment Protocol)

JSEP is the state machine that governs this exchange:

- **Step 1:** One peer (usually the browser) creates an **Offer** (SDP).
- **Step 2:** The browser sends this Offer to the Video Bridge via the
  **Signaling Channel** (our gRPC `RtcServiceV2`).
- **Step 3:** The Video Bridge applies this offer to its local WebRTC engine
  (`webrtc::PeerConnection`), generates an **Answer** (SDP), and sends it back
  to the browser.
- **Step 4:** Both sides trickle **ICE Candidates** to each other as they
  discover them. Once a viable network path is agreed upon, the P2P media
  connection is established.

---

## 3. Internal Components & Data Flow

Once signaling succeeds, the WebRTC engine takes over. The `videobridge` binary
hosts the following internal components to bridge WebRTC to the emulator:

```mermaid
graph TD
    Browser[Browser / Client] <-->|JSEP Signaling| RtcServiceV2[RtcServiceV2]
    RtcServiceV2 <--> Switchboard[Switchboard]
    Switchboard <--> Participant[Participant]

    subgraph WebRTC Engine
        Participant -->|Data Channels| EventForwarder[EventForwarder]
        VideoSource[VideoSource] -->|I420 Frames| Participant
        AudioSource[AudioSource] -->|10ms PCM| Participant
    end

    EventForwarder -->|InputEvent Proto| EmulatorClient[EmulatorClient]
    EmulatorClient -->|streamScreenshot| VideoSource
    EmulatorClient -->|streamAudio| AudioSource

    EmulatorClient <-->|gRPC EmulatorController| Emulator[Android Emulator]
```

### 3.1 `EmulatorClient`

A gRPC client wrapper that connects to the emulator's `EmulatorController`
service. It uses `EmulatorGrpcClientBuilder` to discover the emulator's port and
handles gRPC stream reactors for:

- **`StreamScreenshot`**: Requests a continuous stream of BGR24/RGBA screenshots
  from the emulator.
- **`StreamInputEvent`**: Pushes touch, mouse, and keyboard events back into the
  emulator.

### 3.2 `GrpcVideoSource`

A custom `webrtc::AdaptedVideoTrackSource` that coordinates the screenshot
capture loop. It is decoupled from format-specific details by delegating to a
`VideoFormatPipeline`:

- **Shared Memory (MMAP) Transport:** To achieve 60 FPS, the emulator writes
  frame buffers directly to a shared memory file mapped by the Video Bridge,
  eliminating gRPC serialization overhead. The size of the shared memory region
  is queried from the active pipeline.
- **Fallback Transport:** If shared memory mapping fails, it automatically falls
  back to receiving frames via standard gRPC protobuf byte payloads.
- **Multi-Format Extensibility:** Instead of hardcoding format details, it
  delegates gRPC configuration, shared memory sizing, and `webrtc::VideoFrame`
  construction to the injected `VideoFormatPipeline` strategy.

### 3.3 `VideoFormatPipeline`

An abstraction interface that encapsulates format-specific requirements and
conversions:

- **Format Negotiation:** Defines the gRPC `ImageFormat` to request from the
  emulator.
- **Memory Management:** Calculates the exact bytes required in the shared
  memory region for a given frame dimension.
- **Frame Construction:** Converts raw incoming bytes (e.g., RGBA pixels or
  native GPU handle metadata) and constructs the final `webrtc::VideoFrame` with
  the correct rotation and timestamps.
- **Supported Pipelines:**
  - `RgbaToI420Pipeline`: The default pipeline. It requests `RGBA8888` from the
    emulator, converts it to `I420` YUV on the CPU using `libyuv`, and packages
    it into a standard software-backed `VideoFrame`.
  - _Future Pipelines:_ `RgbaToNv12Pipeline` (for hardware-accelerated encoders)
    and `NativeTexturePipeline` (for zero-copy GPU texture streaming).

### 3.3 `Switchboard` & `Participant`

- **`Participant`**: Represents a single WebRTC connection. It wraps the
  `webrtc::PeerConnection` object, listens to WebRTC connection state changes,
  and configures the audio/video tracks.
- **`Switchboard`**: The orchestrator that manages multiple active `Participant`
  sessions. It acts as the routing table for the signaling layer, maintaining
  thread-safe FIFO message queues for each participant so that gRPC signaling
  requests are safely routed to the correct WebRTC signaling threads.

### 3.4 `EventForwarder`

An observer registered on WebRTC **Data Channels**:

- When the browser sends mouse clicks, key presses, or touch coordinates, they
  arrive over WebRTC SCTP data channels as binary protobuf packets.
- `EventForwarder` intercepts these, deserializes them, and writes them directly
  to the emulator's `StreamInputEvent` gRPC channel to inject input into the
  guest OS with sub-millisecond latency.

### 3.5 Platform Codec Factories & Hardware Acceleration

To deliver low-latency high-resolution video streams (such as `1080x2400` @ 60
FPS) without overloading the host CPU, the `videobridge` leverages
platform-specific hardware acceleration for video encoding and decoding.

The bridge instantiates `CompositeVideoEncoderFactory` and
`CompositeVideoDecoderFactory`. These custom wrappers prioritize the platform's
hardware-accelerated H.264 codecs and gracefully fall back to WebRTC's built-in
software codecs (VP8, VP9, AV1) if the hardware encoder fails or is not
supported.

#### Windows (Media Foundation)

On Windows, the bridge implements a custom `MFVideoEncoderH264` using the
Windows Media Foundation API. It enumerates and instantiates a
hardware-accelerated H.264 Encoder MFT (Media Foundation Transform).

- **Format Conversion:** The encoder converts incoming YUV frames to NV12 using
  `libyuv` and feeds them into the MFT.
- **Performance Optimizations:** To minimize heap allocations in the video
  pipeline's hot path, input and output samples/buffers are pre-allocated during
  initialization and reused for subsequent frames.
- **Threading and Lifecycle:** The Media Foundation MFT operates synchronously.
  To prevent race conditions where resources could be released while encoding is
  active, the `Encode` method holds an exclusive mutex lock for its entire
  duration.

#### macOS (VideoToolbox)

On macOS, the bridge uses `RTCVideoEncoderFactoryH264` and
`RTCVideoDecoderFactoryH264` from Apple's VideoToolbox framework. These are
wrapped into a native `webrtc::VideoEncoderFactory` via the
`webrtc::ObjCToNativeVideoEncoderFactory` bridge.

- **Lifetime Management:** Because Objective-C++ uses Automatic Reference
  Counting (ARC), the native C++ wrapper classes hold strong references
  (`objc_factory_`) to the underlying Objective-C factory objects to prevent
  them from being prematurely deallocated.
- **Level Elevation for High Resolutions:** By default, WebRTC's H.264 factory
  advertises **Level 3.1** (`profile-level-id=42e01f`), which caps the video
  resolution at `1280x720` at 30 FPS. High-resolution streams like `1080x2400`
  exceed the macroblock limits of Level 3.1, causing Apple's VideoToolbox to
  fail with `kVTParameterErr` (`-12902`) on macOS.

  To solve this, the composite factories intercept the advertised formats and
  elevate the H.264 `profile-level-id` to **Level 5.2** (`34` in hex), which
  supports up to 4K resolutions at 60 FPS (e.g. Constrained Baseline Profile is
  elevated to `42e034` and High Profile to `640c34`). This allows the browser to
  negotiate high-resolution streams that can be processed directly by the macOS
  GPU.

#### Linux (NVIDIA / VA-API)

_(Planned)_ Future versions will implement hardware-accelerated H.264 encoding
on Linux using NVIDIA's NVENC or Intel/AMD's VA-API to support GPU-accelerated
video encoding in Linux cloud environments. Currently, Linux hosts fall back to
WebRTC's built-in software codecs.

---

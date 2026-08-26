# Component: WebRTC Video Bridge

**Role:** High-performance WebRTC streaming engine providing zero-copy video/audio streaming and low-latency input event injection for the Android Emulator.
**Location:** `hardware/generic/goldfish/emulator/videobridge`
**Namespace:** `goldfish::videobridge`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `RtcService` | `:rtc_service` | `include/goldfish/videobridge/rtc_service.h` | WebRTC signaling service (`v2::Rtc`) implementing gRPC call negotiation. |
| `InProcessVideoSource` | `:videobridge_core` | `include/goldfish/videobridge/in_process_video_source.h` | Feeds guest display frames directly into WebRTC video tracks without IPC overhead. |
| `InProcessAudioSource` | `:videobridge_core` | `include/goldfish/videobridge/in_process_audio_source.h` | Captures guest audio output and delivers PCM samples to WebRTC audio tracks. |
| `InProcessInputSender` | `:videobridge_core` | `include/goldfish/videobridge/in_process_input_sender.h` | Dispatches touch, keyboard, and mouse events directly to emulator input HALs. |
| `Switchboard` | `:videobridge_core` | `include/goldfish/videobridge/switchboard.h` | Manages WebRTC `PeerConnectionFactory`, ICE candidates, and connected participants. |

## Critical Infrastructure
* **Zero-Copy In-Process Bridge:** Operates directly inside the emulator process, eliminating external IPC serialization and socket overhead for streaming.
* **Hardware-Accelerated Codecs:**
    *   **macOS:** Native VideoToolbox H.264 hardware encoder (`codec_factories_mac.mm`).
    *   **Windows:** Microsoft Media Foundation (MF) hardware H.264 encoder (`mf_video_encoder_h264.cc`).
    *   **Linux / Generic:** WebRTC software VP8/VP9 and OpenH264 encoders.
* **Signaling Protocol:** Implements bidirectional streaming gRPC signaling for SDP offer/answer exchange and ICE candidate trickle.

## Dependencies
* **WebRTC:** `@webrtc` (Core engine, PeerConnection, VideoBroadcaster).
* **gRPC & Protos:** `@aemu//protos/services/webrtc:rtc_service_v2_cc_grpc`, `@grpc//:grpc++`.
* **Translation & Security:** `//emulator/libs/grpc_utils:absl_translate`, `//emulator/libs/grpc_security`.

## Threading Model
* **WebRTC Signaling & Worker Threads:** Uses dedicated WebRTC network, worker, and signaling threads managed by `PeerConnectionFactory`.
* **Frame Delivery Threading:** `InProcessVideoSource::OnFrame` can be called from the render thread; frames are queued and adapted asynchronously on the WebRTC encoder queue.

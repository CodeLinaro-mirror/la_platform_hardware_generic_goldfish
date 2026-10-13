# Component: WebRTC Video Bridge

**Role:** High-performance WebRTC streaming engine providing zero-copy video/audio streaming and low-latency input event injection for the Android Emulator.
**Location:** `hardware/generic/goldfish/emulator/videobridge`
**Namespace:** `goldfish::videobridge`

## Integration Guide
| Class / Interface | Bazel Target | Header / Source Files | Description |
| :--- | :--- | :--- | :--- |
| `RtcService` | `@goldfish//emulator/videobridge:rtc_service` | `include/goldfish/videobridge/rtc_service.h`, `service/rtc_service.cc` | WebRTC signaling service (`v2::Rtc`) implementing gRPC call negotiation and ICE candidate unwrapping (`@goldfish//emulator/videobridge:rtc_service_test`). |
| `InProcessVideoSource` | `@goldfish//emulator/videobridge:videobridge_core` | `include/goldfish/videobridge/in_process_video_source.h`, `core/in_process_video_source.cc` | Feeds guest display frames directly into WebRTC video tracks (`OnFrame`), tracks frame drops (`webrtc_dropped_*`), and performs `libyuv` I420 conversion (`@goldfish//emulator/videobridge:in_process_video_source_test`). |
| `InProcessAudioSource` | `@goldfish//emulator/videobridge:videobridge_core` | `include/goldfish/videobridge/in_process_audio_source.h`, `core/in_process_audio_source.cc` | Captures guest audio output and delivers PCM samples to WebRTC audio tracks. |
| `InProcessInputSender` | `@goldfish//emulator/videobridge:videobridge_core` | `include/goldfish/videobridge/in_process_input_sender.h`, `core/in_process_input_sender.cc` | Dispatches touch, keyboard, mouse, and `sensors_v2` events directly to emulator HALs (`@goldfish//emulator/videobridge:in_process_input_sender_test`). |
| `Participant` & `EventForwarder` | `@goldfish//emulator/videobridge:videobridge_core` | `core/participant.h`, `core/participant.cc`, `core/event_forwarder.h`, `core/event_forwarder.cc` | Creates per-peer WebRTC SCTP `DataChannel` instances (e.g., `input`, `gps`, `sensors_v2`) and forwards incoming protobuf messages to `InputSender` / `InProcessInputSender`. |
| `Switchboard` | `@goldfish//emulator/videobridge:videobridge_core` | `include/goldfish/videobridge/switchboard.h`, `core/switchboard.cc` | Manages WebRTC `PeerConnectionFactory`, ICE candidates, and connected `Participant` sessions. |
| `MFVideoEncoderH264` | `@goldfish//emulator/videobridge:videobridge_win` | `core/win/mf_video_encoder_h264.h`, `core/win/mf_video_encoder_h264.cc` | Windows Media Foundation async H.264 hardware encoder (`InitEncode`, `Encode`, `Release`, `ProcessOutput`, `MF_E_NOTACCEPTING` handling). Tested in `core/win/test/mf_hardware_test.cc` (`@goldfish//emulator/videobridge:mf_hardware_test`) and `core/win/mf_video_encoder_h264_test.cc`. |

## Critical Infrastructure
* **WebRTC DataChannel Vertical Slice (e.g., `sensors_v2`):**
    1.  **WebRTC Peer & DataChannel (`emulator/videobridge`):** `Participant` (`core/participant.h`, `core/participant.cc`) opens SCTP data channels (`sensors_v2`, `input`, `gps`) and attaches `EventForwarder` (`core/event_forwarder.h`, `core/event_forwarder.cc`), which invokes `InputSender` / `InProcessInputSender` (`include/goldfish/videobridge/in_process_input_sender.h`, `core/in_process_input_sender.cc`, tested in `core/in_process_input_sender_test.cc`).
    2.  **In-Process gRPC RTC Wiring (`emulator/plugin/grpc/services`):** `InProcessRtcService` (`//emulator/plugin/grpc/services:in_process_rtc_service`, `include/android/emulation/control/in_process_rtc_service.h`, `in_process_rtc_service.cc`, tested in `in_process_rtc_service_test.cc`) constructs `Switchboard` and `InProcessInputSender` with the emulator's `PhysicalModel` / `InputSession`.
    3.  **Browser / Aquarium Client (`emulator/ui/aquarium` & `@aemu//api/web`):** `emulator/ui/aquarium/example/main.tsx` (`//emulator/ui/aquarium`) and `hardware/google/aemu/api/web/src/core/sensors_channel.ts` (`rtc_controller.ts`, `sensors.ts`) stream high-frequency telemetry over the `sensors_v2` WebRTC data channel.
* **Hardware-Accelerated Codecs:**
    *   **macOS:** Native VideoToolbox H.264 hardware encoder (`codec_factories_mac.mm`).
    *   **Windows:** Microsoft Media Foundation (MF) hardware H.264 encoder (`core/win/mf_video_encoder_h264.h`, `core/win/mf_video_encoder_h264.cc`, hardware test `core/win/test/mf_hardware_test.cc`, target `@goldfish//emulator/videobridge:mf_hardware_test`).
    *   **Linux:** NVIDIA NVENC and Intel/AMD VA-API hardware H.264 encoders with multi-GPU failover and software fallback (`core/linux/`).
* **Signaling Protocol:** Implements bidirectional streaming gRPC signaling for SDP offer/answer exchange and ICE candidate trickle.

## Dependencies
* **WebRTC:** `@webrtc` (Core engine, PeerConnection, VideoBroadcaster).
* **gRPC & Protos:** `@aemu//protos/services/webrtc:rtc_service_v2_cc_grpc`, `@grpc//:grpc++`.
* **Translation & Security:** `//emulator/libs/grpc_utils:absl_translate`, `//emulator/libs/grpc_security`.

## Threading Model
* **WebRTC Signaling & Worker Threads:** Uses dedicated WebRTC network, worker, and signaling threads managed by `PeerConnectionFactory`.
* **Frame Delivery Threading:** `InProcessVideoSource::OnFrame` can be called from the render thread; frames are queued and adapted asynchronously on the WebRTC encoder queue.

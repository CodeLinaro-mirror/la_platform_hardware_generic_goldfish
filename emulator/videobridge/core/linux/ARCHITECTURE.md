# Component: Linux Hardware Video Encoders

**Role:** High-performance hardware-accelerated H.264 video encoding pipeline on Linux using NVIDIA NVENC and Intel/AMD VA-API with composite failover for WebRTC streaming.
**Location:** `hardware/generic/goldfish/emulator/videobridge/core/linux`
**Namespace:** `goldfish::videobridge`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `NvencVideoEncoderFactory` | `:nvenc_video_encoder` | `core/linux/nvenc_video_encoder_factory.h` | WebRTC `VideoEncoderFactory` probing and creating NVIDIA NVENC hardware encoders. |
| `NvencVideoEncoder` | `:nvenc_video_encoder` | `core/linux/nvenc_video_encoder.h` | WebRTC `VideoEncoder` implementation driving NVIDIA NVENC fixed-function silicon. |
| `NvencLoader` | `:nvenc_loader` | `core/linux/nvenc_loader.h` | Dynamic loader resolving `libnvidia-encode.so.1` via `goldfish::os::DynamicLibrary`. |
| `VaapiVideoEncoderFactory` | `:vaapi_video_encoder` | `core/linux/vaapi_video_encoder_factory.h` | WebRTC `VideoEncoderFactory` probing and creating Intel/AMD VA-API hardware encoders. |
| `VaapiVideoEncoder` | `:vaapi_video_encoder` | `core/linux/vaapi_video_encoder.h` | WebRTC `VideoEncoder` implementation driving Intel QuickSync / AMD VCN silicon. |
| `VaapiLoader` | `:vaapi_loader` | `core/linux/vaapi_loader.h` | Dynamic loader resolving `libva.so.2` and probing DRM render nodes (`/dev/dri/renderD128+`). |
| `CopyOrConvertFrameToNv12` | `:nvenc_video_encoder` | `core/linux/frame_nv12_converter.h` | Shared frame ingester with single-burst `std::memcpy` fast-path for contiguous NV12. |

## Critical Infrastructure
* **Zero Link-Time Dependencies:** Dynamically resolves driver libraries (`libnvidia-encode.so.1`, `libva.so.2`, `libva-drm.so.2`) at runtime. If GPU hardware or drivers are unavailable, execution gracefully falls back to built-in software encoders without crashing.
* **Composite Dispatcher & Chained Failover:** `CreateVideoEncoderFactory()` instantiates a `CompositeVideoEncoderFactory` that queries and prioritizes GPU hardware encoders (NVENC $\rightarrow$ VA-API $\rightarrow$ WebRTC software codecs).
* **Unified NV12 Ingestion:** Performs zero-transcode memory copy for native `kNV12` frame buffers with contiguous memory fast paths, preserving `ToI420()` fallback for non-NV12 formats.
* **Infinite GOP Real-Time Streaming:** Operates with `intra_period = 0` (no periodic keyframe interval) and on-demand IDR keyframe generation triggered by RTCP PLI/FIR requests.
* **Runtime Override:** Controlled via `ANDROID_EMU_VIDEO_ENCODER` (`auto`, `nvenc`, `vaapi`, `software`).

## Dependencies
* **Dynamic Library Loader:** `//emulator/libs/os` (`goldfish::os::DynamicLibrary`).
* **WebRTC:** `@webrtc//api/video_codecs:video_codecs_api`, `@webrtc//modules/video_coding:video_codec_interface`.
* **NVENC Prebuilts:** `@goldfish_prebuilts_common//libnvidia:nvenc_headers`.
* **Image Processing:** `@libyuv`.

## Threading Model
* **Encoder TaskQueue:** WebRTC drives `Encode()` sequentially on a dedicated encoder TaskQueue thread.
* **Asynchronous Rate Control:** `SetRates()` is invoked asynchronously from WebRTC network/worker threads based on real-time bandwidth estimation (BWE).
* **Lock-Free Callback Invariant:** All internal state is guarded by `absl::Mutex`. The lock is released before invoking `webrtc::EncodedImageCallback::OnEncodedImage()` to prevent lock-inversion deadlocks with downstream packetizers.

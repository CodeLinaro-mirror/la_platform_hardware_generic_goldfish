# Component: Display Plugin

**Role:** Abstraction layer and reactive frame pipeline for emulator displays, screenshots,
multi-display management, and WebM video recording. **Location:** `emulator/plugin/display/`
**Namespace:** `goldfish::display`
**Agent Playbook:** [`AGENTS.md`](AGENTS.md)

## Integration Guide

| Class / Interface                 | Bazel Target                                                | Header Path                                         | Description                                                                                                     |
| :-------------------------------- | :---------------------------------------------------------- | :-------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------- |
| `IDisplay`                        | `@goldfish//emulator/plugin/display:base`                   | `include/goldfish/display/display.h`                | Base display interface exposing `DisplaySource` frame pipelines, screenshot capture, and input event injection. |
| `PixmanDisplay`                   | `@goldfish//emulator/plugin/display:primary_display`        | `include/goldfish/display/pixman_display.h`         | Display implementation backed by a `pixman_image_t` and CPU `DisplayPipeline`.                                  |
| `QemuDisplay`                     | `@goldfish//emulator/plugin/display:primary_display`        | `qemu_display.h`                                    | Primary QEMU `DisplayChangeListener` (`dcl`) implementation bridging guest surfaces to `DisplaySource`.         |
| `AbstractMultiDisplay`            | `@goldfish//emulator/plugin/display:abstract_multi_display` | `include/goldfish/display/abstract_multi_display.h` | Interface for managing primary and secondary displays.                                                          |
| `VirtualDisplay` / `MultiDisplay` | `@goldfish//emulator/plugin/display:display`                | `include/goldfish/display/virtual_display.h`        | Secondary virtual display implementation and multi-display registry.                                            |
| `VideoRecorder` / `WebMEncoder`   | `@goldfish//emulator/plugin/display:video_recorder`         | `include/goldfish/display/video_recorder.h`         | Asynchronous `DisplaySink` consumer that encodes display frames into WebM (VP8/VP9) via FFmpeg.                 |

## Critical Infrastructure

- **Reactive Display Pipeline (`//emulator/libs/display_pipeline`):**
  - `IDisplay` acts as a `DisplaySource` producer, publishing immutable `DisplayFrame` buffers
    through CPU transformation stages (`//emulator/libs/display_pipeline:cpu_pipeline`) to
    registered `DisplaySink` consumers.
  - Consumers (gRPC screenshot streams, `VideoRecorder`, UI surfaces) subscribe as `DisplaySink`
    listeners without blocking the producer thread.
- **Asynchronous Video Recording (`:video_recorder`):**
  - `VideoRecorder` attaches to a `DisplaySource`, queues incoming `DisplayFrame` payloads, and
    encodes them on a dedicated worker thread via `WebMEncoder` (`@ffmpeg`).

## Dependencies

- **Core:** `@abseil-cpp//absl/status`, `@abseil-cpp//absl/time`.
- **Goldfish:** `//emulator/libs/display_pipeline:core`,
  `//emulator/libs/display_pipeline:cpu_pipeline`, `//emulator/libs/base`,
  `//emulator/libs/fps_calculator`, `//emulator/libs/async:async_api`,
  `//emulator/plugin/hal/multidisplay`.
- **External:** `@pixman//:pixman-1`, `@libpng//:png`, `@ffmpeg//libavcodec:avcodec`,
  `@ffmpeg//libavformat:avformat`, `@ffmpeg//libswscale:swscale`.

## Threading Model

- **Thread Safe:** `IDisplay` methods (`seq()`, `waitForFrame()`, `DisplaySource` subscription) are
  thread-safe.
- **Non-Blocking Producer Contract:** Frame updates from QEMU BQL (`dpy_gfx_update`) or gfxstream
  `PostWorker` must never block on slow consumers; `VideoRecorder` offloads encoding to its
  background worker thread.

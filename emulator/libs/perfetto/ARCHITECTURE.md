# Component: Perfetto Tracing (`perfetto`)

**Role:** Core Library / Zero-Cost Compile-Time Tracing Integration
**Location:** `emulator/libs/perfetto/`
**Build Targets:** `@goldfish//emulator/libs/perfetto:init`, `@goldfish//emulator/libs/perfetto:init_test`

## Overview
Provides Perfetto track-event tracing across the Android Emulator (`hardware/generic/goldfish`), including QEMU plugin trace hooks, display pipeline instrumentation, WebRTC `videobridge` frame tracking, and gRPC `PerfettoInterceptor` spans.

By default, Perfetto tracing is compiled out for zero runtime overhead unless the Bazel flag `--@qemu//:trace_backend=goldfish` (matching `@qemu//:goldfish_enabled`) is enabled, which defines the preprocessor macro `CONFIG_TRACE_GOLDFISH`.

## Key Files & Symbols
- **`include/goldfish/perfetto/perfetto.h`** & **`perfetto_config.cc`**:
  - `goldfish::perfetto::Initialize()` — Initializes the Perfetto SDK track-event backend (`PERFETTO_TRACK_EVENT_STATIC_STORAGE()`).
  - `goldfish::perfetto::EmulatorTracingSession` (`Start`, `Stop`) — Manages active tracing sessions when `ANDROID_EMU_PERFETTO_TRACE` is set at runtime.
- **`include/goldfish/perfetto/perfetto_categories.h`**:
  - `PERFETTO_DEFINE_CATEGORIES(...)` — Registers the `"rendering"`, `"async"`, `"grpc"`, `"qemu"`, and `"display"` trace categories when `CONFIG_TRACE_GOLDFISH` is defined.
  - `TRACE_NONCE()` — Returns a process-wide monotonically increasing `uint64_t` flow nonce (`::perfetto::internal::NextTraceNonce()`) when `CONFIG_TRACE_GOLDFISH` is defined, or `0` when disabled.
  - When `!defined(CONFIG_TRACE_GOLDFISH)`, defines zero-cost compile-time no-op macros (`GOLDFISH_PERFETTO_NOOP`, `perfetto::internal::IgnoreTraceArgs`) for `TRACE_EVENT`, `TRACE_EVENT_BEGIN`, `TRACE_EVENT_END`, `TRACE_EVENT_INSTANT`, `TRACE_COUNTER`, `perfetto::Flow::ProcessScoped`, and defines `TRACE_EVENT_CATEGORY_ENABLED(category)` as `(false)`.
- **`perfetto_config_test.cc`**:
  - Unit tests (`@goldfish//emulator/libs/perfetto:init_test`) verifying trace session initialization and category gating.

## Cross-Component Integration
- **QEMU Plugin Startup (`@goldfish//emulator/plugin:goldfish_x86_64` / `emulator/plugin/plugin.cc`)**: `start_tracing()` and `stop_tracing()` (guarded by `CONFIG_TRACE_GOLDFISH`) register `OnQemuGoldfishTrace` via `qemu_goldfish_set_trace_callback` to bridge QEMU trace events into the `"qemu"` category.
- **gRPC Interceptor (`@goldfish//emulator/plugin/grpc/interceptors:perfetto_interceptor`)**: `PerfettoInterceptor` and `PerfettoInterceptorFactory` (`emulator/plugin/grpc/interceptors/perfetto_interceptor.cc`) check `TRACE_EVENT_CATEGORY_ENABLED("grpc")` before instantiating per-RPC trace spans.
- **Display & WebRTC (`@goldfish//emulator/plugin/display`, `@goldfish//emulator/videobridge`)**: Frame sequence drops (`dropped_frames`, `FrameDropped`, `webrtc_dropped_frames`, `WebRTCFrameDropped`) and `grpc_frame_interval_ms` are guarded by `TRACE_EVENT_CATEGORY_ENABLED("display")`.

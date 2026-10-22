# Display Plugin & Frame Pipeline (`//emulator/plugin/display/...` & `//emulator/libs/display_pipeline/...`) — Agent Playbook

**Namespace:** `goldfish::display` (test doubles: `goldfish::display::test` and `goldfish::display::testing`; conformance helpers: `goldfish::display::conformance`)

---

## 1. Key Targets & Types

| Component | Bazel Target | Header | Role |
| :--- | :--- | :--- | :--- |
| `FrameProducer`, `DisplayFrames`, `SlotPool`, `LatestFramePump` | `//emulator/libs/display_pipeline:core` | [frame_producer.h](../../libs/display_pipeline/include/goldfish/display/frame_producer.h), [display_frames.h](../../libs/display_pipeline/include/goldfish/display/display_frames.h), [slot_pool.h](../../libs/display_pipeline/include/goldfish/display/slot_pool.h), [latest_frame_pump.h](../../libs/display_pipeline/include/goldfish/display/latest_frame_pump.h) | Capture into pooled slots; consumers receive immutable frames (`FramePtr = std::shared_ptr<const SurfaceFrame>`). `LatestFramePump` coalesces frame events and processes only the newest frame on the consumer's worker. |
| `LegacySurfaceSource`, `CpuReadPipeline()` | `//emulator/libs/display_pipeline:cpu_pipeline` | [legacy_surface_source.h](../../libs/display_pipeline/include/goldfish/display/legacy_surface_source.h), [cpu_read_pipeline.h](../../libs/display_pipeline/include/goldfish/display/cpu_read_pipeline.h), [cpu_slot_storage.h](../../libs/display_pipeline/include/goldfish/display/cpu_slot_storage.h) | `LegacySurfaceSource` owns the `SlotPool`. `CpuReadPipeline()` is a free function (libyuv) for format conversion, scaling, and rotation. |
| `IDisplay` | `//emulator/plugin/display:base` | [display.h](include/goldfish/display/display.h) | Frame listeners, `GetPixels` (PNG via `:write_png`), input injection (`:ev_dev_event`). |
| `PixmanDisplay`, `QemuDisplay` | `//emulator/plugin/display:primary_display` | [pixman_display.h](include/goldfish/display/pixman_display.h), [qemu_display.h](qemu_display.h) | `PixmanDisplay` is the generic pixman-image display; `QemuDisplay` (a `PixmanDisplay` subclass) owns the QEMU `DisplayChangeListener`. |
| `IMultiDisplay`, `VirtualDisplay` | `//emulator/plugin/display:abstract_multi_display`, `//emulator/plugin/display:display` | [abstract_multi_display.h](include/goldfish/display/abstract_multi_display.h), [virtual_display.h](include/goldfish/display/virtual_display.h) | Display registry, created by `IMultiDisplay::Create(EventLoop* loop, EventLoop* qemu_loop, const sensors::FoldableConfig* fc)`. `VirtualDisplay` copies gfxstream color buffers. |
| `VideoRecorder`, `WebMEncoder` | `//emulator/plugin/display:video_recorder` | [video_recorder.h](include/goldfish/display/video_recorder.h), [webm_encoder.h](include/goldfish/display/webm_encoder.h) | Pull-based: `VideoRecorder`'s worker thread calls your `DrawCallback` (RGB24 buffer) `fps` times a second, and `WebMEncoder`'s thread writes VP9 WebM at half resolution via `@ffmpeg`. The encoder queue is unbounded and `Finish()` drops frames still queued. |
| Test doubles | `//emulator/plugin/display:test-display`, `//emulator/libs/display_pipeline:testing` | [fake_pixman_display.h](include/goldfish/display/test/fake_pixman_display.h), [fake_multi_display.h](include/goldfish/display/test/fake_multi_display.h), [fake_slot_storage.h](../../libs/display_pipeline/include/goldfish/display/test/fake_slot_storage.h) | `test::FakePixmanDisplay` / `ActiveFakePixmanDisplay` / `FakeMultiDisplay`; `testing::FakeSlotStorage`; `conformance::ReferenceRead` and the `Make*Frame` helpers (`read_pipeline_conformance.h`). |

---

## 2. Threading, Slot & Lifetime Rules

1. **Producers never block.** `QemuDisplay`'s listener callbacks (`grpc_dpy_gfx_update` / `grpc_dpy_gfx_switch` → `QemuDisplay::OnGuestFrame` / `OnSurfaceSwitch`) run on the QEMU main loop under the BQL; `OnGuestFrame` → `FrameProducer::Invalidate()` may capture inline on that thread, and `RequestFrame()` pumps run on the producer's executor (`qemu_loop` for `QemuDisplay` / `VirtualDisplay`, the display loop for plain `PixmanDisplay` / `FakePixmanDisplay`). `VirtualDisplay` copies gfxstream color buffers in `CaptureSurface` on `qemu_loop`. None of these may encode, do disk I/O, or wait for consumers.
2. **Consumers read on their own thread.** Never call `SurfaceFrame::ReadInto`, `WaitForFreshFrame` / `WaitForFrame`, or `IDisplay::GetPixels` (waits up to 500 ms for a fresh frame, then reads and encodes synchronously) on `qemu_loop` or the display event loop.
3. **Slots are never overwritten.** Each `LegacySurfaceSource` owns a `SlotPool` of `slot_count` (default 4) slots per geometry. A slot returns to the pool only when its last `FramePtr` is dropped (there is no release call), and `DisplayFrames` pins one slot as its latest frame. When every slot is leased, capture returns `Start::kNoSlot` and the frame is skipped (nothing is overwritten, allocated, or blocked on); the next slot release triggers `RequestFrame()` to recapture. Don't hold `FramePtr`s longer than needed.
4. **`PixmanImagePtr` adopts.** `PixmanImagePtr` (= `goldfish::base::IntrusivePtr<pixman_image_t>`, [pixman_image_ptr.h](include/goldfish/display/pixman_image_ptr.h)) takes over the reference it is constructed with; use it only for images you own (e.g. a fresh `pixman_image_create_bits`). For borrowed images such as QEMU's `surface->image`, use `SharePixmanImage(img)`. `PixmanImagePtr` copies and destruction and `SharePixmanImage` take `details::g_pixman_mutex`; QEMU's own `pixman_image_ref` / `unref` (e.g. in `qemu_free_displaysurface`) do not.
5. **Surface swaps.** `PixmanDisplay::pixman_mutex_` guards `source_image_` between surface replacement (`OnSurfaceSwitch` / `OnGuestFrame` / `UpdateSourceImage`) and capture. It is acquired before `FrameProducer`'s `publish_mutex_`. Consumers never read `source_image_`.
6. **Teardown.** An `IDisplay` / `PixmanDisplay` subclass that overrides producer hooks (`OnDemand`, `OnFrameRequested`, source hooks) must call `ShutdownProducer()` (or `StopProducerCalls()` plus `frame_producer_->Shutdown()`) first in its destructor. Destroying or resetting a `ScopedFrameListener` waits for its in-flight callback, so never do it while holding a lock that callback takes.

---

## 3. Testing

- Drive interleavings deterministically: `testing::FakeSlotStorage` (`CompleteNext()`, `DropNext()`, `FailNextAllocation(absl::Status)`) with `TestEventLoop::RunAll()`. Models: [latest_frame_pump_test.cc](../../libs/display_pipeline/latest_frame_pump_test.cc) and `OwedCaptureDuringUpdateSourceImageDoesNotSelfDeadlock` in [fake_pixman_display_test.cc](fake_pixman_display_test.cc).
- To inject a producer reset between acquire and read, call `FakePixmanDisplay::ResetProducer()` from `RunDuringNextResize()` during a scaled `getScreenshot` / `streamScreenshot` (`GetPixels` never triggers the hook); see `StreamSurvivesProducerReset` in [display_service_test.cc](../grpc/services/emulator_controller_methods/display_service_test.cc).
- Add one cross-thread test like `WiredToDisplayFramesListenerAcrossThreads` (in `latest_frame_pump_test.cc`). Both tests in `pixman_display_concurrency_test.cc` are `DISABLED_`; don't model on them.

```bash
bazel test --test_output=errors @goldfish//emulator/plugin/display:all @goldfish//emulator/libs/display_pipeline:all
bazel test --config=tsan --test_output=errors @goldfish//emulator/plugin/display:display_unittests @goldfish//emulator/libs/display_pipeline:core_test @goldfish//emulator/libs/display_pipeline:cpu_pipeline_test
```

# Component: QEMU Audio Plugin

**Role:** Captures guest audio streams from QEMU and routes PCM audio samples into the WebRTC in-process audio bridge.
**Location:** `hardware/generic/goldfish/emulator/plugin/audio`
**Namespace:** `goldfish::audio`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `QemuAudioCapture` | `:qemu_audio_capture` | `include/goldfish/audio/qemu_audio_capture.h` | Low-level QEMU audio subsystem hook and voice capture manager. |
| `QemuAudioSource` | `:qemu_audio_source` | `include/goldfish/audio/qemu_audio_source.h` | WebRTC audio source adapter delivering captured audio samples to WebRTC tracks. |

## Critical Infrastructure
* **QEMU Audio Subsystem Integration:** Attaches to QEMU voice output structures (`SWVoiceOut`) under the VM lock.
* **Format Conversion:** Resamples and formats PCM streams (16-bit interleaved stereo) for consumption by WebRTC audio tracks.
* **VM Synchronization:** Enforces `ScopedVmLock` when registering or modifying audio capture callbacks with QEMU internals.

## Dependencies
* **QEMU Headers:** `@qemu//:aemu_func_defs`, `@qemu//:qemu-headers-exported`.
* **Videobridge:** `//emulator/videobridge:in_process_audio_source`, `//emulator/videobridge:videobridge_core`.
* **VM Interface:** `//emulator/plugin/vminterface:vm_lock`.

## Threading Model
* **Audio Thread Safety:** QEMU audio callbacks run on QEMU audio worker threads; samples are marshalled to the WebRTC audio looper safely via thread-safe buffers.

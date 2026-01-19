# Component: Android Logging

**Role:** Provides logging utilities, including a colorful terminal sink and a bridge for legacy C code.
**Location:** `hardware/generic/goldfish/android/logging`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ColorLogSink` | `:color_log_sink` | `.../color_log_sink.h` | An `absl::LogSink` that outputs ANSI-colored logs to stderr/stdout. |
| `ALOG...` | `:absl_c_bridge` | `.../abseil_log_bridge.h` | Macros (`ALOGI`, `ALOGE`) bridging C-style logging to Abseil. |

## Critical Infrastructure
* **ColorLogSink:** Formats Abseil log entries with colors based on severity (Red=Error, Yellow=Warning) if the output stream is a TTY.
* **C Bridge:** Provides `_log_to_abseil` to allow legacy C code (common in QEMU and older Android components) to log via the modern C++ logging backend (`absl::log`).

## Dependencies
* **Core:** `@abseil-cpp//absl/log`.

## Threading Model
* **Thread Safe:** Logging is inherently thread-safe via Abseil's internal locking. `ColorLogSink` writes to `std::ostream`, which is synchronized.

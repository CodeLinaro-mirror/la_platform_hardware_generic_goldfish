# Component: ADB Logger

**Role:** A sniffer that logs ADB protocol packets traversing the emulator's `adb-vsock` connection and records them in circular breadcrumb buffers.
**Location:** `hardware/generic/goldfish/emulator/plugin/adb-vsock/logger`
**Namespace:** `goldfish::adb`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AdbLogger` | `:logger` | `.../adb_message_logger.h` | Implements `IDataSniffer` to log bidirectional ADB traffic. |
| `AdbBreadcrumbTracker` | `:logger` | `.../adb_breadcrumb_tracker.h` | Implements `AdbPacketCallback` to log ADB traffic to the circular breadcrumb buffer. |

## Critical Infrastructure
* **Sniffer:** Inherits `goldfish::devices::cable::IDataSniffer`. When attached to a Socket/Plug pair (like in `adb-vsock`), it intercepts all data flowing in both directions (`ToSocket`, `ToPlug`).
* **Parser:** `AdbMessageLogger` parses the raw byte stream into ADB messages (`amessage` struct: `CNXN`, `AUTH`, `OPEN`, `OKAY`, `WRTE`, `CLSE`).
* **Output:** Logs the parsed messages to `LOG(INFO)` with a prefix indicating direction (e.g., `adb: H->G: OPEN ...`).
* **Breadcrumb Tracking:** `AdbBreadcrumbTracker` listens to parsed ADB messages and records them as `Breadcrumb` protos in a circular buffer for crash reporting.
    * **Stateful Tracking:** Tracks up to 16 active streams (32 entries for bidirectional flows) to associate commands with services (e.g., "sync:", "shell:") using a `flow_id`.
    * **Buffer Protection:** Excludes heavy data payloads on high-volume streams (like `sync:`) to prevent circular buffer exhaustion.
    * **Memory Efficiency:** Uses `absl::InlinedVector<..., 32>` to track open streams without heap allocations in the common case.

## Dependencies
* **Connector:** `//emulator/plugin/hal/connector` (For `IDataSniffer`).
* **Crashreport:** `//emulator/crashreport:breadcrumb` (For circular log).
* **Base:** `@abseil-cpp//absl/log` and `@abseil-cpp//absl/container:inlined_vector`.

## Threading Model
* **Thread Safe:** The sniffer methods (`ToSocket`, `ToPlug`) are called synchronously by the I/O thread handling the socket. `AdbBreadcrumbTracker` uses an `absl::Mutex` (`open_streams_mutex_`) to protect the state of open streams across concurrent events.

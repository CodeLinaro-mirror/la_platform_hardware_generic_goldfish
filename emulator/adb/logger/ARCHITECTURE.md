# Component: ADB Logger

**Role:** A sniffer that logs ADB protocol packets traversing the emulator's `adb-vsock` connection.
**Location:** `hardware/generic/goldfish/emulator/adb/logger`
**Namespace:** `goldfish::adb`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AdbLogger` | `:logger` | `.../adb_message_logger.h` | Implements `IDataSniffer` to log bidirectional ADB traffic. |

## Critical Infrastructure
* **Sniffer:** Inherits `goldfish::devices::cable::IDataSniffer`. When attached to a Socket/Plug pair (like in `adb-vsock`), it intercepts all data flowing in both directions (`ToSocket`, `ToPlug`).
* **Parser:** `AdbMessageLogger` parses the raw byte stream into ADB messages (`amessage` struct: `CNXN`, `AUTH`, `OPEN`, `OKAY`, `WRTE`, `CLSE`).
* **Output:** Logs the parsed messages to `LOG(INFO)` with a prefix indicating direction (e.g., `adb: H->G: OPEN ...`).

## Dependencies
* **Connector:** `//emulator/hal/connector` (For `IDataSniffer`).
* **Base:** `@abseil-cpp//absl/log`.

## Threading Model
* **Thread Safe:** The sniffer methods (`ToSocket`, `ToPlug`) are called synchronously by the I/O thread handling the socket. `AdbMessageLogger` maintains internal state (bytes in flight) but assumes serial execution per direction.

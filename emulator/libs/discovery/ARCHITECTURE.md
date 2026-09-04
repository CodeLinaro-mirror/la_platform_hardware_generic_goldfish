# Component: Emulator Discovery & Advertisement

**Role:** Manages discovery files (`pid_<pid>.ini`) that advertise running emulator instances, gRPC endpoints, auth tokens, and netsimd endpoints to host tools (like Android Studio and test runners).
**Location:** `hardware/generic/goldfish/emulator/libs/discovery`
**Namespace:** `goldfish::discovery`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `EmulatorAdvertisement` | `:emulator_advertisement` | `include/goldfish/discovery/emulator_advertisement.h` | Parses and writes discovery `.ini` files for active emulator processes. |

## Critical Infrastructure
* **Discovery Directory:** Writes to `$ANDROID_EMULATOR_HOME/discovery/` or OS-specific discovery directory.
* **Advertisement Properties:** Advertises `grpc.port`, `grpc.token`, `grpc.jwks_path`, `netsim.endpoint`, `pid`, and `serial.port`.
* **Process Lifecycle Binding:** Discovery files are removed upon clean shutdown or cleaned up on stale PID detection.

## Dependencies
* **Filesystem & Config:** `//emulator/libs/ini_file`, `//emulator/libs/system`.
* **Networking:** `//emulator/libs/sockets`.
* **Abseil:** `@abseil-cpp//absl/status:statusor`, `@abseil-cpp//absl/strings`.

## Threading Model
* **Thread Safe:** Stateless file parsing methods and atomic write helpers are thread-safe.

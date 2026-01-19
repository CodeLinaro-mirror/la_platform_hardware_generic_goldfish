# Component: gRPC ADB Service

**Role:** Provides a gRPC interface for ADB-related host operations.
**Location:** `hardware/generic/goldfish/emulator/grpc/services/adb`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `getAdbService()` | `//emulator/grpc/services/adb/server:adb-service` | `.../adb_service.h` | Factory to create the ADB service instance. |

## Critical Infrastructure
* **Methods:**
    *   `pullAdbKey`: Reads the private ADB key from the host's filesystem (`~/.android/adbkey` via `getPrivateAdbKeyPath`) and returns it to the client. This is used by the web-based emulator console to authenticate with the guest's `adbd`.

## Dependencies
* **Core:** `//emulator/adb/secrets` (Key path lookup).
* **External:** `@grpc//:grpc++`.

## Threading Model
* **Thread Safe:** Stateless service implementation. Executed on gRPC completion queue threads.

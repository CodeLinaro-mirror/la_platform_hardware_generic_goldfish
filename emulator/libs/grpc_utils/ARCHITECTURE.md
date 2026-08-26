# Component: gRPC Abseil Utils

**Role:** Provides translation utilities between Abseil primitives and gRPC equivalents.
**Location:** `hardware/generic/goldfish/emulator/libs/grpc_utils`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AbslStatusToGrpcStatus` | `:absl_translate` | `include/android/emulation/control/absl_status_translate.h` | Helper to map `absl::Status` to `grpc::Status`. |
| `GrpcStatusToAbslStatus` | `:absl_translate` | `include/android/emulation/control/absl_status_translate.h` | Helper to map `grpc::Status` to `absl::Status`. |

## Critical Infrastructure
* **Interoperability:** Used by gRPC service implementations to return errors consistent with internal emulator logic (which heavily uses Abseil).

## Dependencies
* **Core:** `@abseil-cpp//absl/status`.
* **External:** `@grpc//:grpc++`.

## Threading Model
* **Thread Safe:** Stateless conversion functions.

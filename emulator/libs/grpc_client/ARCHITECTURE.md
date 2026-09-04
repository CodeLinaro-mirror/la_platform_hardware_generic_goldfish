# Component: Emulator gRPC Client

**Role:** High-level C++ client library for connecting to and controlling the Android Emulator via gRPC.
**Location:** `hardware/generic/goldfish/emulator/libs/grpc_client`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `EmulatorGrpcClientBuilder` | `:grpc_client` | `include/android/emulation/control/emulator_grpc_client.h` | Builder for constructing client instances. |
| `BlockingEmulatorGrpcClient` | `:grpc_client` | `include/android/emulation/control/emulator_grpc_client.h` | Synchronous client for simple tasks. |
| `CallbackEmulatorGrpcClient` | `:grpc_client` | `include/android/emulation/control/emulator_grpc_client.h` | Asynchronous client with connection monitoring. |

## Critical Infrastructure
* **Discovery Support:** `EmulatorGrpcClientBuilder` can parse emulator advertisement files (`.ini`). It automatically extracts the port, authentication token, and TLS settings.
* **Connection Monitoring:** `CallbackEmulatorGrpcClient` runs a background thread to monitor connection health and fires events via `ConnectionStateChanges()`.
* **Security:** Automatically configures `ClientContext` with Bearer tokens or TLS certificates based on the endpoint description.
* **Channel Factory:** `GrpcChannelFactory` manages the underlying `grpc::Channel` lifecycle, handling TLS credentials and custom arguments.

## Dependencies
* **Core:** `@grpc//:grpc++`, `@abseil-cpp`.
* **Protos:** `@aemu//protos/client:grpc_endpoint_description_cc_proto`.
* **Utilities:** `//emulator/libs/ini_file`, `//emulator/libs/discovery:emulator_advertisement`, `//emulator/libs/grpc_security`.

## Threading Model
* **Blocking Client:** No internal threads. All operations (Connect, Disconnect) block the caller.
* **Callback Client:** Manages a background worker thread for connection monitoring and state maintenance. Subscriptions to `ConnectionStateChanges` are thread-safe.

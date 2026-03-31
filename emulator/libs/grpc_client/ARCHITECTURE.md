# Component: Emulator gRPC Client

**Role:** High-level C++ client library for connecting to and controlling the Android Emulator via gRPC.
**Location:** `hardware/generic/goldfish/emulator/grpc/client`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `EmulatorGrpcClientBuilder` | `:emulator_grpc_client` | `.../emulator_grpc_client.h` | Builder for constructing client instances. |
| `BlockingEmulatorGrpcClient` | `:emulator_grpc_client` | `.../emulator_grpc_client.h` | Synchronous client for simple tasks. |
| `CallbackEmulatorGrpcClient` | `:emulator_grpc_client` | `.../emulator_grpc_client.h` | Asynchronous client with connection monitoring. |

## Critical Infrastructure
* **Discovery Support:** `EmulatorGrpcClientBuilder` can parse emulator advertisement files (`.ini`). It automatically extracts the port, authentication token, and TLS settings.
* **Connection Monitoring:** `CallbackEmulatorGrpcClient` runs a background thread to monitor connection health and fires events via `ConnectionStateChanges()`.
* **Security:** Automatically configures `ClientContext` with Bearer tokens or TLS certificates based on the endpoint description.
* **Channel Factory:** `GrpcChannelFactory` manages the underlying `grpc::Channel` lifecycle, handling TLS credentials and custom arguments.

## Dependencies
* **Core:** `@grpc//:grpc++`, `@abseil-cpp`.
* **Protos:** `@aemu//protos/client:grpc_endpoint_description_proto`.
* **Utilities:** `//emulator/libs/files:ini_file` (for discovery).

## Threading Model
* **Blocking Client:** No internal threads. All operations (Connect, Disconnect) block the caller.
* **Callback Client:** Manages a background worker thread for connection monitoring and state maintenance. Subscriptions to `ConnectionStateChanges` are thread-safe.

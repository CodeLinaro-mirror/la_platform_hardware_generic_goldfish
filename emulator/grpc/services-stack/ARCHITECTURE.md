# Component: gRPC Services Stack

**Role:** Aggregator and configurator for the emulator's gRPC server. It bundles services, security, and interceptors into a single manageable unit.
**Location:** `hardware/generic/goldfish/emulator/grpc/services-stack`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `EmulatorControllerService::Builder` | `:services-stack` | `.../grpc_services.h` | Fluent builder for configuring and starting the gRPC server. |

## Critical Infrastructure
* **Server Lifecycle:** `EmulatorControllerServiceImpl` owns the `grpc::Server` instance and ensures graceful shutdown.
* **Security Composition:** The builder coordinates:
    *   **Transport Security:** TLS setup via `withCertAndKey`.
    *   **Authentication:** Orchestrates `StaticTokenAuth` and `JwtTokenAuth` using `AnyTokenAuth`.
    *   **Authorization:** Integrates `AllowList` into the gRPC metadata processor.
* **Interceptor Injection:** Automatically adds `StdOutLoggingInterceptor` and `IdleInterceptor` (if timeout is configured) to the server.
* **Port Discovery:** Searches for an available TCP port within a given range to bind the server.

## Dependencies
* **Services:** `//emulator/grpc/services/adb/server`, `//emulator/grpc/services/emulator_controller/server`.
* **Security:** `//emulator/grpc/security`.
* **Interceptors:** `//emulator/grpc/interceptors`.
* **External:** `@grpc//:grpc++`.

## Threading Model
* **Thread Safe:** The builder is intended for use during startup. The resulting `EmulatorControllerService` handle is thread-safe for `stop()` and `wait()` operations.

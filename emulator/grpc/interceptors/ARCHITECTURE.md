# Component: gRPC Interceptors

**Role:** Provides cross-cutting concerns for gRPC services, including logging, lifecycle management, and crash diagnostics.
**Location:** `hardware/generic/goldfish/emulator/grpc/interceptors`
**Namespace:** `android::control::interceptor`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `LoggingInterceptor` | `:interceptors` | `.../logging_interceptor.h` | Records RPC method, latency, and status. |
| `IdleInterceptor` | `:interceptors` | `.../idle_interceptor.h` | Shuts down emulator after period of inactivity. |
| `BreadcrumbInterceptor` | `:interceptors` | `.../breadcrumb_interceptor.h` | Adds RPC state to crash report annotations. |

## Critical Infrastructure
* **Observability:** `LoggingInterceptor` generates `InvocationRecord` structs. `StdOutLoggingInterceptorFactory` provides a default implementation that writes to `LOG(INFO)`.
* **Lifecycle:** `IdleInterceptor` uses a `goldfish::async::EventLoop::Timer` to periodically check if any RPCs were active. If the "Termination Time" is exceeded, it triggers an orderly shutdown.
* **Diagnostics:** `BreadcrumbInterceptor` encodes the active gRPC method and its phase into a CRC-checksummed, base64 string. These are stored in the crash reporter's breadcrumb buffer to help debug crashes occurring during RPC execution.

## Dependencies
* **Core:** `//android/async` (EventLoop), `//emulator/libs/debug`.
* **External:** `@grpc//:grpc++`.

## Threading Model
* **Thread Safe:** Interceptors are invoked by the gRPC stack on its completion queue threads. The `IdleInterceptor` uses atomic counters to track active requests across threads.

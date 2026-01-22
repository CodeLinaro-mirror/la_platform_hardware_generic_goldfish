# Component: gRPC Service Forwarder

**Role:** Routes gRPC calls from the emulator's main gRPC endpoint to external service backends.
**Location:** `hardware/generic/goldfish/emulator/grpc/services/forwarder`
**Namespace:** `android::emulation::forwarding`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ServiceForwarderImpl` | `:service_forwarder` | `.../service_forwarder_impl.h` | Implementation of the `ServiceForwarder` gRPC service. |
| `UiControllerForwarder` | `:ui_controller_forwarder` | `.../ui_controller_forwarder.h` | Specialized router for UI controller services. |

## Critical Infrastructure
* **Dynamic Routing:** `ServiceForwarderImpl` maintains a thread-safe map (`rules_`) of service URIs to remote `Endpoint`s. External processes can call `registerForwarder` to announce themselves as the handler for a specific service.
* **Inter-process Communication:** Enables modularity by allowing specific emulator features (like a custom UI or a specialized hardware emulator) to run in a separate process while still being reachable via the emulator's primary gRPC port.

## Dependencies
* **External:** `@grpc//:grpc++`, `@abseil-cpp`.
* **Protos:** `@aemu//protos/services/forwarder:service_forwarder_cc_grpc`.

## Threading Model
* **Thread Safe:** `ServiceForwarderImpl` uses `absl::Mutex` to protect the routing table, allowing concurrent registrations and lookups.

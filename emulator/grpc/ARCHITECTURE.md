# Component: Emulator gRPC Framework

**Role:** The modular gRPC framework for the Android Emulator, providing remote control, observation, and inter-process communication.
**Location:** `hardware/generic/goldfish/emulator/grpc`
**Namespace:** `android::emulation::control`, `android::emulation::forwarding`

## Library Index

### Core Framework
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **async** | High-level templates for gRPC's callback-based async API. | [Architecture](async/ARCHITECTURE.md) |
| **absl** | Translation between Abseil and gRPC status codes. | [Architecture](absl/ARCHITECTURE.md) |
| **interceptors** | Logging, Idle detection, and Crash diagnostics. | [Architecture](interceptors/ARCHITECTURE.md) |
| **security** | JWT/JWKS verification and JSON-based authorization. | [Architecture](security/ARCHITECTURE.md) |

### Services
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **emulator_controller** | The main control hub (VM, Sensors, Display, Input). | [Architecture](services/emulator_controller/ARCHITECTURE.md) |
| **adb** | Host ADB key retrieval service. | [Architecture](services/adb/ARCHITECTURE.md) |
| **forwarder** | Dynamic RPC routing to external process backends. | [Architecture](services/forwarder/ARCHITECTURE.md) |
| **event-stream** | Bridging internal events to gRPC server streams. | [Architecture](event-stream/ARCHITECTURE.md) |

### Integration & Client
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **services-stack** | The "Glue" that aggregates services and security. | [Architecture](services-stack/ARCHITECTURE.md) |
| **client** | High-level C++ clients with discovery and monitoring. | [Architecture](client/ARCHITECTURE.md) |

## High-Level Architecture
The gRPC stack is designed to be highly modular. Services are implemented independently and then bundled together by the `services-stack` builder. Security and observability are handled transparently via interceptors and a centralized auth metadata processor.

## Flows & Guides
* [Request Flow](docs/request_flow.md)

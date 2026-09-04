# Component: Emulator Libraries

**Role:** A collection of modular, reusable C++ libraries powering the Goldfish emulator.
**Location:** `hardware/generic/goldfish/emulator/libs`
**Namespace:** `goldfish::*`

## Library Index

### Core & OS Abstractions
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **base** | Smart pointers (`IntrusivePtr`), RAII (`UniqueHandle`). | [Architecture](base/ARCHITECTURE.md) |
| **system** | Abstraction layer for OS services (time, env, system info). | [Architecture](system/ARCHITECTURE.md) |
| **process** | Process management, subprocess spawning, and thread control. | [Architecture](process/ARCHITECTURE.md) |
| **os** | OS abstractions (Dynamic library loading). | [Architecture](os/ARCHITECTURE.md) |
| **debug** | Logging and assertion macros. | [Architecture](debug/ARCHITECTURE.md) |
| **logging** | Structured logging primitives and level management. | [Architecture](logging/ARCHITECTURE.md) |
| **cpp** | C++ meta-programming helpers (`Overloaded`). | [Architecture](cpp/ARCHITECTURE.md) |
| **status_macros** | Abseil status macros and status conversion helpers. | [Architecture](status_macros/ARCHITECTURE.md) |

### Async, I/O & Communication
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **async** | Platform-independent async I/O (libuv, QEMU event loop). | [Architecture](async/ARCHITECTURE.md) |
| **sockets** | Cross-platform BSD socket abstractions and loopback utilities. | [Architecture](sockets/ARCHITECTURE.md) |
| **network** | Netlink parsing and IOVectors. | [Architecture](network/ARCHITECTURE.md) |
| **stream** | Stream I/O adapters and buffering. | [Architecture](stream/ARCHITECTURE.md) |
| **SocketBuffer** | Auto-resizing ring buffer for streaming I/O. | [Architecture](SocketBuffer/ARCHITECTURE.md) |
| **eventing** | Reactive primitives (`ObservableValue`). | [Architecture](eventing/ARCHITECTURE.md) |
| **broadcasting** | Type-safe Pub/Sub (`Topic`, `Ticket`). | [Architecture](broadcasting/ARCHITECTURE.md) |
| **QEMUBH** | C++ wrapper for QEMU Bottom Halves. | [Architecture](QEMUBH/ARCHITECTURE.md) |

### Data Structures, Files & Config
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **hardware_config** | Hardware properties parser and memory layout configuration. | [Architecture](hardware_config/ARCHITECTURE.md) |
| **file_system_watcher** | Cross-platform directory monitoring and filesystem notifications. | [Architecture](file_system_watcher/ARCHITECTURE.md) |
| **proto_data_store** | Atomic, fixed-size circular storage for Protobuf messages. | [Architecture](proto_data_store/ARCHITECTURE.md) |
| **archive** | Serialization (`IReader`/`IWriter`) with VarInt encoding. | [Architecture](archive/ARCHITECTURE.md) |
| **parsing** | String splitting, key-value parsing, and type-safe argument streams (`ArgStream`). | [Architecture](parsing/ARCHITECTURE.md) |
| **UniqueIdAllocator** | ID generation and recycling. | [Architecture](UniqueIdAllocator/ARCHITECTURE.md) |
| **ext4** | Utilities for creating and resizing ext4 filesystem images. | [Architecture](ext4/ARCHITECTURE.md) |

### gRPC & Security
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **grpc_client** | Emulator gRPC client builder and connection monitor. | [Architecture](grpc_client/ARCHITECTURE.md) |
| **grpc_security** | Token authentication, allowlists, and JWK management for gRPC. | [Architecture](grpc_security/ARCHITECTURE.md) |
| **grpc_utils** | Status code translation and async gRPC bridge utilities. | [Architecture](grpc_utils/ARCHITECTURE.md) |
| **adbkey** | ADB key generation, parsing, and authentication keypair management. | [Architecture](adbkey/ARCHITECTURE.md) |

### Graphics, Hardware & Physics
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **gvk** | Goldfish Vulkan wrappers and dispatch tables. | [Architecture](gvk/ARCHITECTURE.md) |
| **imaging** | Pixel formats and image data containers. | [Architecture](imaging/ARCHITECTURE.md) |
| **fps_calculator** | Moving average FPS calculation. | [Architecture](fps_calculator/ARCHITECTURE.md) |
| **sensors** | Sensor simulation logic and foldable state. | [Architecture](sensors/ARCHITECTURE.md) |
| **physics** | Physical environment simulation (Inertial, Ambient). | [Architecture](physics/ARCHITECTURE.md) |
| **cpu** | Host CPU feature detection and core topologies. | [Architecture](cpu/ARCHITECTURE.md) |
| **shared_memory** | Cross-platform shared memory mapping. | [Architecture](shared_memory/ARCHITECTURE.md) |

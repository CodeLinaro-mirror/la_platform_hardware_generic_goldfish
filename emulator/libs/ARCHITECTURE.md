# Component: Emulator Libraries

**Role:** A collection of modular, reusable C++ libraries powering the Goldfish emulator.
**Location:** `hardware/generic/goldfish/emulator/libs`
**Namespace:** `goldfish::*`

## Library Index

### Core & Foundations
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **base** | Smart pointers (`IntrusivePtr`), RAII (`UniqueHandle`). | [Architecture](base/ARCHITECTURE.md) |
| **debug** | Logging and assertion macros. | [Architecture](debug/ARCHITECTURE.md) |
| **cpp** | C++ meta-programming helpers (`Overloaded`). | [Architecture](cpp/ARCHITECTURE.md) |
| **os** | OS abstractions (Dynamic Library loading). | [Architecture](os/ARCHITECTURE.md) |

### Eventing & Communication
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **eventing** | Reactive primitives (`ObservableValue`). | [Architecture](eventing/ARCHITECTURE.md) |
| **broadcasting** | Type-safe Pub/Sub (`Topic`, `Ticket`). | [Architecture](broadcasting/ARCHITECTURE.md) |
| **QEMUBH** | C++ wrapper for QEMU Bottom Halves. | [Architecture](QEMUBH/ARCHITECTURE.md) |

### Data Structures & Algorithms
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **SocketBuffer** | Auto-resizing ring buffer for streaming I/O. | [Architecture](SocketBuffer/ARCHITECTURE.md) |
| **proto_data_store** | Atomic, fixed-size circular storage for Protobuf messages. | [Architecture](proto_data_store/ARCHITECTURE.md) |
| **archive** | Serialization (`IReader`/`IWriter`) with VarInt encoding. | [Architecture](archive/ARCHITECTURE.md) |
| **UniqueIdAllocator** | ID generation and recycling. | [Architecture](UniqueIdAllocator/ARCHITECTURE.md) |
| **parsing** | String splitting and key-value parsing. | [Architecture](parsing/ARCHITECTURE.md) |

### Graphics & Display
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **display** | Display abstraction (`IDisplay`) and Pixman implementation. | [Architecture](display/ARCHITECTURE.md) |
| **gvk** | Goldfish Vulkan wrappers and dispatch tables. | [Architecture](gvk/ARCHITECTURE.md) |
| **imaging** | Pixel formats and image data containers. | [Architecture](imaging/ARCHITECTURE.md) |
| **fps_calculator** | Moving average FPS calculation. | [Architecture](fps_calculator/ARCHITECTURE.md) |

### Emulation Logic
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **avd_universe** | State model of the AVD (Sensors, Clipboard, etc.). | [Architecture](avd_universe/ARCHITECTURE.md) |
| **sensors** | Sensor simulation logic and foldable state. | [Architecture](sensors/ARCHITECTURE.md) |
| **physics** | Physical environment simulation (Inertial, Ambient). | [Architecture](physics/ARCHITECTURE.md) |
| **network** | Netlink parsing and IOVectors. | [Architecture](network/ARCHITECTURE.md) |
| **shared_memory** | Cross-platform shared memory mapping. | [Architecture](shared_memory/ARCHITECTURE.md) |

### Legacy / Shims
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **QEMUFile_h** | C header shim for QEMU migration types. | [Architecture](QEMUFile_h/ARCHITECTURE.md) |

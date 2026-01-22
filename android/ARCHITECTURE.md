# Android Support Libraries

**Role:** A collection of cross-platform C++ libraries providing system abstractions, logging, and common utilities used by the Goldfish emulator.
**Location:** `hardware/generic/goldfish/android`
**Namespace:** `android::base`, `goldfish::*`

## Library Index

### System & OS Abstraction
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **system** | OS interface (Time, Env, Paths) and File utilities. | [Architecture](system/ARCHITECTURE.md) |
| **process** | Process spawning, monitoring, and output capture. | [Architecture](process/ARCHITECTURE.md) |
| **cpu** | Host CPU capability and virtualization detection. | [Architecture](cpu/ARCHITECTURE.md) |
| **files** | File system watching and INI file parsing. | [Architecture](files/ARCHITECTURE.md) |
| **memory** | Memory tracking and paging hints (`madvise`). | [Architecture](memory/ARCHITECTURE.md) |
| **filesystems** | Ext4 image creation and resizing tools. | [Architecture](filesystems/ARCHITECTURE.md) |

### Networking & IPC
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **network** | IP/Endpoint abstractions and DNS resolution. | [Architecture](network/ARCHITECTURE.md) |
| **sockets** | Cross-platform BSD socket wrappers. | [Architecture](sockets/ARCHITECTURE.md) |
| **async** | Asynchronous I/O primitives (documented in `hardware/generic/goldfish/android/async`). | [Architecture](async/ARCHITECTURE.md) |

### Logging & Diagnostics
| Library | Description | Documentation |
| :--- | :--- | :--- |
| **logging** | ANSI color logging and legacy C-bridge. | [Architecture](logging/ARCHITECTURE.md) |
| **status_macros** | `RETURN_IF_ERROR` macros for Abseil Status. | [Architecture](status_macros/ARCHITECTURE.md) |

## Design Philosophy
These libraries aim to provide a modern, C++17/20 interface while abstracting away platform differences between Linux, macOS, and Windows. They prefer **Abseil** types (`absl::Status`, `absl::Time`) over custom implementations whenever possible.

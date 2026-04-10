# Component: Hardware Abstraction Layers (HALs)

**Role:** Implements the host-side logic for various emulated Android hardware devices.
**Location:** `hardware/generic/goldfish/emulator/hal`
**Namespace:** `goldfish::devices`

## Infrastructure
The HALs communicate with the guest using a shared infrastructure stack:
1.  **Connector (`emulator/hal/connector`):** The "Physical Layer" defining Plugs and Sockets.
2.  **Plug (`emulator/hal/plug`):** The thread-safe "Device Layer" base class (`HalPlug`) that isolates HALs from QEMU threading.
3.  **Transport (`emulator/plugin/vsock_goldfish`):** The virtio-vsock backend moving data between Guest and Host.

## Device Index

### Core Services
| HAL | Role | Protocol | Documentation |
| :--- | :--- | :--- | :--- |
| **bootproperties** | Serves build props to `init`. | QEMUD | [Architecture](bootproperties/ARCHITECTURE.md) |
| **guest-status** | Tracks boot lifecycle. | QEMUD | [Architecture](guest-status/ARCHITECTURE.md) |
| **clipboard** | Syncs clipboard text. | Custom | [Architecture](clipboard/ARCHITECTURE.md) |

### Sensors & Input
| HAL | Role | Protocol | Documentation |
| :--- | :--- | :--- | :--- |
| **sensors** | Accelerometer, Gyro, etc. | QEMUD Sensors | [Architecture](sensors/ARCHITECTURE.md) |
| **fingerprint** | Fingerprint reader. | QEMUD | [Architecture](fingerprint/ARCHITECTURE.md) |
| **gps** | Location provider. | QEMUD GPS | [Architecture](gps/ARCHITECTURE.md) |

### Multimedia & IO
| HAL | Role | Protocol | Documentation |
| :--- | :--- | :--- | :--- |
| **camera** | Webcams and virtual scenes. | Goldfish Camera | [Architecture](camera/ARCHITECTURE.md) |
| **unix_pipe** | Proxy to Host UDS. | Raw Stream | [Architecture](unix_pipe/ARCHITECTURE.md) |

### Shared
| Library | Role | Documentation |
| :--- | :--- | :--- |
| **common** | Shared interfaces (Reset). | [Architecture](common/ARCHITECTURE.md) |
| **connector** | Device Registry & Cable. | [Architecture](connector/ARCHITECTURE.md) |
| **plug** | Thread-safe HAL Base Class. | [Architecture](plug/ARCHITECTURE.md) |

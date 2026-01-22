# Component: Emulator Plugins

**Role:** Extensions and Devices that plug into the QEMU emulator core to provide Android-specific functionality.
**Location:** `hardware/generic/goldfish/emulator/plugin`
**Namespace:** `goldfish::*`

## Plugin Index

### Infrastructure & Transport
| Plugin | Role | Documentation |
| :--- | :--- | :--- |
| **vsock_goldfish** | Virtio-Vsock backend (Host-Guest transport). | [Architecture](vsock_goldfish/ARCHITECTURE.md) |
| **netsim** | WiFi/BT/UWB simulation bridge to `netsimd`. | [Architecture](netsim/ARCHITECTURE.md) |
| **adb-vsock** | Bridges Guest `adbd` to Host via VSOCK. | [Architecture](adb-vsock/ARCHITECTURE.md) |

### Virtual Hardware
| Plugin | Role | Documentation |
| :--- | :--- | :--- |
| **battery** | Emulated Battery (MMIO). | [Architecture](battery/ARCHITECTURE.md) |
| **virtio-input-android** | Multi-touch input device (Virtio). | [Architecture](virtio-input-android/ARCHITECTURE.md) |
| **virtio-wifi** | Emulated WiFi device (Virtio). | [Architecture](virtio-wifi/ARCHITECTURE.md) |

### Control & Management
| Plugin | Role | Documentation |
| :--- | :--- | :--- |
| **avd** | Bootstrap device (`avdstart`), HAL registration. | [Architecture](avd/ARCHITECTURE.md) |
| **grpc** | Emulator Controller gRPC service. | [Architecture](grpc/ARCHITECTURE.md) |
| **vminterface** | VM Control API (`VmOperations`). | [Architecture](vminterface/ARCHITECTURE.md) |
| **system** | System abstractions (Time). | [Architecture](system/ARCHITECTURE.md) |

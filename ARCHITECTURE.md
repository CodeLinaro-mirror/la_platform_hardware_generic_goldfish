# Goldfish Project Architecture

**Role:** The root of the Android Emulator (Goldfish) project source tree. This repository contains the source code for the emulator's launcher, QEMU plugins, virtual hardware (HALs), and support libraries.
**Location:** `hardware/generic/goldfish`
**Namespace:** `android::goldfish`

## Documentation Index

### Top-Level Components
| Component | Description | Documentation |
| :--- | :--- | :--- |
| **[Emulator](emulator/ARCHITECTURE.md)** | The core emulator implementation. Includes the launcher, QEMU plugins, HALs, libraries, and control services (gRPC). | [Architecture](emulator/ARCHITECTURE.md) |
| **[Development](development/ARCHITECTURE.md)** | Developer tools, VS Code settings, and task runners. | [Architecture](development/ARCHITECTURE.md) |
| **[Third Party](third_party/ARCHITECTURE.md)** | Vendored external libraries (e.g., Sparse Image, ext4_utils). | [Architecture](third_party/ARCHITECTURE.md) |

## Project Structure

*   **`emulator/`**: Contains the business logic, launcher, QEMU plugins, and support libraries.
    *   **`launcher/`**: The `emulator` binary entry point.
    *   **`plugin/`**: Dynamic libraries loaded by QEMU (`goldfish_*.so`), including virtual hardware devices, HALs, and the gRPC control service.
    *   **`libs/`**: Cross-platform C++ support libraries (System, Process, Network, Logging, Async I/O) used across components.
*   **`development/`**: Developer tools, build scripts, and editor configuration.
*   **`third_party/`**: External code that isn't managed via Bazel external repositories.

## Getting Started

For build instructions, usage guides, and IDE setup, please refer to the project [README](README.MD).

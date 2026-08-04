# Goldfish Emulator Architecture

**Role:** The top-level component for the Android Emulator (Goldfish). This directory orchestrates the build, packaging, and integration of all emulator sub-components.
**Location:** `hardware/generic/goldfish/emulator`
**Namespace:** N/A (Build root)

## System Overview

The Goldfish emulator is a modular system built on top of QEMU. It extends QEMU with Android-specific virtual hardware, a custom UI/Control service (gRPC), and a specialized launcher.

### Core Components

| Component | Description | Documentation |
| :--- | :--- | :--- |
| **[Launcher](launcher/ARCHITECTURE.md)** | The main entry point (`emulator`). Configures QEMU arguments, handles port allocation, and spawns the QEMU process. | [Architecture](launcher/ARCHITECTURE.md) |
| **[Plugins](plugin/ARCHITECTURE.md)** | QEMU extensions (Transport, Hardware, Control) loaded at runtime. Includes `virtio-goldfish-*` devices. | [Architecture](plugin/ARCHITECTURE.md) |
| **[HALs](hal/ARCHITECTURE.md)** | Host-side implementations of Android Hardware Abstraction Layers (Sensors, GPS, Camera, etc.). | [Architecture](hal/ARCHITECTURE.md) |
| **[gRPC](grpc/ARCHITECTURE.md)** | The control plane. Provides remote control, observability, and inter-process communication via gRPC. | [Architecture](grpc/ARCHITECTURE.md) |
| **[Libraries](libs/ARCHITECTURE.md)** | Shared C++ libraries (Async I/O, Logging, Math, Config) used across all components. | [Architecture](libs/ARCHITECTURE.md) |

### Supporting Infrastructure

| Component | Description | Documentation |
| :--- | :--- | :--- |
| **[Config](config/ARCHITECTURE.md)** | Hardware configuration parsing (`config.ini`) and environment discovery (SDK/AVD paths). | [Architecture](config/ARCHITECTURE.md) |
| **[Cmdline](cmdline/ARCHITECTURE.md)** | Command-line argument parsing and validation. | [Architecture](cmdline/ARCHITECTURE.md) |
| **[Crashreport](crashreport/ARCHITECTURE.md)** | Crashpad integration and hang detection. | [Architecture](crashreport/ARCHITECTURE.md) |
| **[SDK](sdk/ARCHITECTURE.md)** | Local mock SDK environment for testing. | [Architecture](sdk/ARCHITECTURE.md) |
| **[Tests](tests/ARCHITECTURE.md)** | Integration tests (Boot, CTS, dEQP). | [Architecture](tests/ARCHITECTURE.md) |
| **[Tools](tools/ARCHITECTURE.md)** | Build-time utilities (Versioning, Packaging). | [Architecture](tools/ARCHITECTURE.md), [Fishtank Update Guide](tools/upload_fishtank/README.md) |

## Build & Release

The root `BUILD.bazel` file orchestrates the final assembly of the emulator package:

*   **Binaries:** Collects `emulator` (launcher), `qemu-system-*` (from external repo), `crashpad_handler`, and `netsimd`.
*   **Plugins:** Collects all dynamic libraries (`goldfish_*.so`, `rutabaga_ffi.so`) into `lib/qemu/`.
*   **Graphics:** Bundles host-specific Vulkan ICDs and EGL libraries (ANGLE, SwiftShader) into `lib64/`.
*   **Resources:** Bundles QEMU ROMs, keymaps, and virtual scene data.
*   **Packaging:** Creates the final release ZIP (`sdk-repo-{platform}-emulator-{build_id}.zip`) and symbol packages.

## Architectural Constraints

*   **QEMU Dependency:** The core emulation engine is QEMU. Most Goldfish logic exists as plugins or sidecar processes communicating via shared memory or sockets.
*   **Cross-Platform:** The codebase must compile and run on Linux, macOS (x86_64/arm64), and Windows.
*   **Bazel:** The build system is Bazel, ensuring hermetic builds and clear dependency graphs.

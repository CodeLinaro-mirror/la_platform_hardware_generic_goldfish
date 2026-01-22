# Emulator Startup Lifecycle

This document details the initialization sequence of the `emulator` launcher binary. It coordinates the setup of the environment, helper processes, and the core QEMU engine.

## Flow Diagram

```mermaid
sequenceDiagram
    participant User
    participant Launcher as Launcher (C++)
    participant Config as Config System
    participant Crash as Crashpad
    participant Netsim as Netsimd Process
    participant QEMU as QEMU Process

    User->>Launcher: main(argc, argv)

    Note over Launcher: 1. Parse Arguments
    Launcher->>Launcher: android_parse_options()

    Note over Launcher: 2. Setup Logging
    Launcher->>Launcher: configureLogging()

    Note over Launcher: 3. Resolve Paths
    Launcher->>Config: ResolvePaths()
    Config-->>Launcher: SDK/AVD/Binary Paths

    Note over Launcher: 4. Initialize Crash Reporting
    Launcher->>Crash: crashhandler_init()
    Crash-->>Launcher: (Starts crashpad_handler)

    Note over Launcher: 5. Load AVD
    Launcher->>Config: Avd::FromName()
    Config-->>Launcher: Avd Object (HW Config)

    Note over Launcher: 6. Setup Networking (Netsim)
    alt Netsim Enabled
        Launcher->>Netsim: Launch(netsimd)
        Launcher->>Launcher: Wait for gRPC Port
        Netsim-->>Launcher: Port Available
    end

    Note over Launcher: 7. Configure QEMU
    Launcher->>Launcher: Emulator::launch_config()
    Launcher->>Launcher: Add Devices (Virtio, GPU, gRPC)
    Launcher->>Launcher: Generate Cmdline

    Note over Launcher: 8. Launch QEMU
    Launcher->>QEMU: Launch(qemu-system-x86_64 ...)

    loop Monitoring
        Launcher->>QEMU: Monitor Process
        Launcher->>Netsim: Monitor Process
    end

    opt Shutdown / Signal
        Launcher->>QEMU: SIGTERM
        Launcher->>Netsim: SIGTERM
    end
```

## Key Phases

1.  **Environment Resolution:** Determining where the SDK, system images, and AVD data reside is critical and complex due to the variety of supported platforms and directory structures.
2.  **Process Orchestration:** The launcher acts as a supervisor. It spawns `netsimd` and `qemu` and ensures they are cleaned up when the emulator exits.
3.  **Device Configuration:** The `Emulator` class translates high-level AVD properties (e.g., "Pixel 4", "API 34") into granular QEMU command-line arguments (`-device virtio-input`, `-drive ...`).

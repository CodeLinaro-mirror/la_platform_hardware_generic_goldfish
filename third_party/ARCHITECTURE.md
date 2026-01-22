# Component: Third Party Libraries

**Role:** External libraries vendored or adapted for the Goldfish emulator.
**Location:** `hardware/generic/goldfish/third_party`
**Namespace:** Various

## Library Index

| Library | Description | Documentation |
| :--- | :--- | :--- |
| **sparse** | Android Sparse Image library. | [Architecture](sparse/ARCHITECTURE.md) |
| **windows** | Windows compatibility headers (mman). | N/A |
| **rust** | Rust crates/bindings (if applicable). | N/A |

## Note on External Dependencies
Most external dependencies (e.g., gRPC, Abseil, QEMU) are fetched via Bazel external repositories (`WORKSPACE`/`MODULE.bazel`) rather than being vendored here. This directory contains code that requires specific modifications or build system integration for Goldfish.

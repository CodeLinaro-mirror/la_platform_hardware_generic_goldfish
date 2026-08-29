# Component: Emulator Tools

**Role:** Provides build-time utilities and rules for versioning, packaging, and symbol management.
**Location:** `hardware/generic/goldfish/emulator/tools`
**Namespace:** N/A (Build rules and scripts)

## Integration Guide
| Target | Description |
| :--- | :--- |
| `:aemu_version` | C++ library exposing version constants (`VERSION`, `BUILD_ID`). Generated from `version_template.h.in`. |
| `grpc-web` | In-process C++ gRPC-Web proxy bridging browser clients to emulator gRPC services. See **[Architecture](grpc-web/ARCHITECTURE.md)** and **[Design](grpc-web/DESIGN.md)**. |
| `symbol_zipper.py` | Python script to aggregate and zip Breakpad/Crashpad symbols for release. |
| `upload_fishtank` | Tool to upload pre-built "fishtank" zips to GCS. See **[Fishtank Update Guide](upload_fishtank/README.md)**. |

## Critical Infrastructure
* **Version Generation:** `version_info.bzl` and `version_info_header` rule generate `aemu_version.h` at build time, injecting the semantic version string and build ID.
* **Packaging:** `packaging.bzl` defines rules like `aemu_naming` to standardize artifact names (e.g., `emulator-darwin_aarch64-0.0.1.zip`).
* **gRPC-Web Proxy:** `grpc-web` tool provides an embedded HTTP/1.1 to gRPC bridge using `llhttp` and `ClientBidiReactor`.
* **Symbol Processing:** Tools to process debug symbols for crash reporting integration.
* **Fishtank Updates:** Managed via the `upload_fishtank` tool and manual updates to `MODULE.bazel`. Detailed instructions are in the `upload_fishtank/README.md`.

## Dependencies
* **Build System:** Heavily relies on Starlark rules (`.bzl`) and Python scripts invoked by Bazel.
* **Networking & gRPC:** Used by `grpc-web` (`llhttp`, `grpc++`, `goldfish::async`).

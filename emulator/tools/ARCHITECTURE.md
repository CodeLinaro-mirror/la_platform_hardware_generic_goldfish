# Component: Emulator Tools

**Role:** Provides build-time utilities and rules for versioning, packaging, and symbol management.
**Location:** `hardware/generic/goldfish/emulator/tools`
**Namespace:** N/A (Build rules and scripts)

## Integration Guide
| Target | Description |
| :--- | :--- |
| `:aemu_version` | C++ library exposing version constants (`VERSION`, `BUILD_ID`). Generated from `version_template.h.in`. |
| `symbol_zipper.py` | Python script to aggregate and zip Breakpad/Crashpad symbols for release. |

## Critical Infrastructure
* **Version Generation:** `version_info.bzl` and `version_info_header` rule generate `aemu_version.h` at build time, injecting the semantic version string and build ID.
* **Packaging:** `packaging.bzl` defines rules like `aemu_naming` to standardize artifact names (e.g., `emulator-darwin_aarch64-0.0.1.zip`).
* **Symbol Processing:** Tools to process debug symbols for crash reporting integration.

## Dependencies
* **Build System:** Heavily relies on Starlark rules (`.bzl`) and Python scripts invoked by Bazel.

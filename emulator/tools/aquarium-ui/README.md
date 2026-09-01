# Aquarium UI Server (`aquarium-ui`)

> **Note**: This standalone server is a **prototype** designed to develop and
> validate our modern TypeScript SDK/API and the Web → gRPC-Web interaction
> patterns for both emulated (Android Emulator / Cuttlefish) and physical
> Android devices.

## Overview

`aquarium-ui` provides a lightweight, unified development host that integrates:

1. **Static Web Serving**: Serves the compiled React/TypeScript Aquarium web
   application (`//emulator/ui/aquarium:dist`) directly from Bazel runfiles,
   with automatic MIME type resolution and Single-Page Application (SPA)
   fallback routing.
1. **In-Process gRPC-Web Proxy**: Bridges browser gRPC-Web calls
   (`POST /android.emulation.control.*`) directly to upstream device gRPC
   services over the same HTTP port, eliminating CORS complexity and cross-port
   overhead.

______________________________________________________________________

## Quick Start

### 1. Launch an Android Emulator

Ensure an emulator instance is running with its gRPC service enabled:

```bash
emulator -avd <avd_name>
```

### 2. Run the Aquarium UI Server

```bash
bazel run @goldfish//emulator/tools/aquarium-ui:aquarium-ui
```

By default:

- Automatically detects the first running emulator using discovery `.ini` files
  in the user's runtime directory.
- Binds the web server to `http://0.0.0.0:8085`.
- Serves the hermetic Aquarium web UI from runfiles.

Navigate to **http://localhost:8085** in your browser to view the emulator
stream and interact with controls.

______________________________________________________________________

## Usage Scenarios

### Connecting to a Specific Emulator

When running multiple emulators, specify an explicit gRPC target address and
optional auth token:

```bash
bazel run @goldfish//emulator/tools/aquarium-ui:aquarium-ui -- \
    --grpc_target localhost:8554 \
    --grpc_token <auth_token>
```

Or target a specific emulator instance via its discovery file:

```bash
bazel run @goldfish//emulator/tools/aquarium-ui:aquarium-ui -- \
    --discovery_file ~/.android/avd/running/pid_12345.ini
```

### Connecting to Physical Devices

For physical devices exposing an upstream gRPC service (or via a device
bridge/daemon):

```bash
bazel run @goldfish//emulator/tools/aquarium-ui:aquarium-ui -- \
    --grpc_target 192.168.1.50:8554 \
    --http_port 8085
```

### Rapid Frontend Iteration

During web development in `//emulator/ui/aquarium`, point `--static_dir` to a
local build directory without needing to recompile the C++ host:

```bash
bazel run @goldfish//emulator/tools/aquarium-ui:aquarium-ui -- \
    --static_dir /path/to/custom/dist
```

______________________________________________________________________

## Command Line Flags

| Flag | Default | Description |
| :----------------- | :-------- | :-------------------------------------------------------------------------------- |
| `--http_address` | `0.0.0.0` | IP/host address to bind the HTTP server. |
| `--http_port` | `8085` | HTTP port to listen on. |
| `--grpc_target` | `""` | Upstream gRPC server (`host:port` or `unix:path`). If unset, uses auto-discovery. |
| `--discovery_file` | `""` | Path to emulator discovery `.ini` file. |
| `--grpc_token` | `""` | Authentication token for upstream gRPC calls. |
| `--static_dir` | `""` | Custom directory containing web UI assets. Defaults to Bazel runfiles. |
| `--allow_origin` | `*` | Value for `Access-Control-Allow-Origin` CORS header. |
| `--access_log` | `false` | Enable structured HTTP request access logging. |
| `--verbose` | `false` | Enable verbose diagnostic logging. |

______________________________________________________________________

## Running Tests

Run the unit test suite and clang-tidy verification:

```bash
bazel test @goldfish//emulator/tools/aquarium-ui:all
```

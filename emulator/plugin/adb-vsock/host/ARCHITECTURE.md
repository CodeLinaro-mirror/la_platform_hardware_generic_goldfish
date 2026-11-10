# Component: ADB Host

**Role:** Interacts with the local ADB Server running on the host machine. **Location:**
`emulator/plugin/adb-vsock/host/` **Namespace:** `goldfish::adb`

## Integration Guide

| Class / Interface | Bazel Target | Header Path             | Description                               |
| :---------------- | :----------- | :---------------------- | :---------------------------------------- |
| `AdbHostServer`   | `:host`      | `.../adb_host_server.h` | Static utilities to talk to `adb server`. |

## Critical Infrastructure

- **Notification:** `AdbHostServer::notify` sends the `host:emulator:<port>` command to the ADB
  server (default port 5037). This tells ADB that a new emulator instance is running on
  `localhost:<port>`, causing it to show up in `adb devices`.
- **Server Shutdown:** `AdbHostServer::killServer` sends `host:kill` to a running ADB server (used
  when host ADB keys are regenerated so a stale daemon drops cached keys).
- **Port Discovery:** `getClientPort` determines the ADB server port, checking environment variables
  (`ANDROID_ADB_SERVER_PORT`).

## Dependencies

- **System:** `//emulator/libs/sockets` (Socket connection to ADB server), `//emulator/libs/system`.
- **Status:** `//emulator/libs/status_macros`, `@abseil-cpp//absl/status`.

## Threading Model

- **Blocking:** `notify` blocks while connecting and sending data to the ADB server.

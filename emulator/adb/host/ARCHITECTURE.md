# Component: ADB Host

**Role:** Interacts with the local ADB Server running on the host machine.
**Location:** `hardware/generic/goldfish/emulator/adb/host`
**Namespace:** `goldfish::adb`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AdbHostServer` | `:host` | `.../adb_host_server.h` | Static utilities to talk to `adb server`. |

## Critical Infrastructure
* **Notification:** `AdbHostServer::notify` sends the `host:emulator:<port>` command to the ADB server (default port 5037). This tells ADB that a new emulator instance is running on `localhost:<port>`, causing it to show up in `adb devices`.
* **Port Discovery:** `getClientPort` determines the ADB server port, checking environment variables (`ANDROID_ADB_SERVER_PORT`).

## Dependencies
* **System:** `//emulator/libs/sockets` (Socket connection to ADB server).
* **Base:** `@aemu//base:aemu-base`.

## Threading Model
* **Blocking:** `notify` blocks while connecting and sending data to the ADB server.

# Component: Emulator Telnet Interface

> [!WARNING]
> **Support Status:** The telnet-based interface is not fully supported and is not considered a secure mechanism for interacting with the emulator. Users should be aware of potential security issues and protocol limitations. This component is maintained primarily for legacy compatibility and is not recommended for production or sensitive environments.

**Role:** Provides a telnet-based command-line interface for emulator control, including authentication, hierarchical command registration, and asynchronous execution.
**Location:** `hardware/generic/goldfish/emulator/telnet`
**Namespace:** `goldfish::telnet`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `TelnetAuth` | `:telnet_auth` | `telnet_auth.h` | Manages authentication tokens and access status. |

## Critical Infrastructure
* **Secure Authentication:** `TelnetAuth` ([telnet_auth.cc](telnet_auth.cc)) manages the telnet security token (typically stored in `~/.emulator_console_auth_token`).

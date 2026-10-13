# Component: Emulator Telnet Interface

> [!WARNING]
> **Support Status:** The telnet-based interface is not fully supported and is not considered a secure mechanism for interacting with the emulator. Users should be aware of potential security issues and protocol limitations. This component is maintained primarily for legacy compatibility and is not recommended for production or sensitive environments.

**Role:** Provides a telnet-based command-line interface for emulator control, including authentication, hierarchical command registration, and asynchronous execution.
**Location:** `hardware/generic/goldfish/emulator/telnet`
**Namespace:** `goldfish::telnet`

## Integration Guide
| Class / Interface | Bazel Target | Header / Source Files | Description |
| :--- | :--- | :--- | :--- |
| `CommandRegistry` | `@goldfish//emulator/telnet:command_registry` | `command_registry.h`, `command_registry.cc` | Hierarchical telnet command tree (`RegisterCommand`, `ExecuteCommand`, `FindCommand`). Tested in `command_registry_test.cc` (`@goldfish//emulator/telnet:command_registry_test`). |
| `LegacyConsoleBridge` | `@goldfish//emulator/telnet:legacy_console_bridge` | `legacy_console_bridge.h`, `legacy_console_bridge.cc` | Bridges legacy telnet commands to `CommandRegistry` and records command execution telemetry via `goldfish::metrics::MetricsReporter` (`Report`, `android_studio::EmulatorDetails`). Tested in `legacy_console_bridge_test.cc` (`@goldfish//emulator/telnet:legacy_console_bridge_test`) using `FakeMetricsWriter`. |
| `LegacyConsoleController` | `@goldfish//emulator/telnet:legacy_console_controller` | `legacy_console_controller.h`, `legacy_console_controller.cc` | Manages telnet console session lifecycle and command dispatch (`@goldfish//emulator/telnet:legacy_console_controller_test`). |
| `ConsoleServer` | `@goldfish//emulator/telnet:console_server` | `console_server.h`, `console_server.cc` | Async TCP server accepting telnet console connections (`@goldfish//emulator/telnet:console_server_test`). |
| `TelnetAuth` | `@goldfish//emulator/telnet:telnet_auth` | `telnet_auth.h`, `telnet_auth.cc` | Manages authentication tokens (`~/.emulator_console_auth_token`) and access status (`@goldfish//emulator/telnet:telnet_auth_test`). |

## Critical Infrastructure & Cross-Package Flows
* **Telnet Metrics Telemetry (`LegacyConsoleBridge` $\leftrightarrow$ `MetricsReporter`):**
  - When a user executes a telnet command, `LegacyConsoleBridge` (`legacy_console_bridge.cc`) invokes `CommandRegistry::ExecuteCommand` and reports usage telemetry through `goldfish::metrics::MetricsReporter` (`//emulator/libs/metrics:metrics_reporter`, header `emulator/libs/metrics/include/goldfish/metrics/metrics_reporter.h`).
  - Unit tests in `legacy_console_bridge_test.cc` (`@goldfish//emulator/telnet:legacy_console_bridge_test`) inject `goldfish::metrics::testing::FakeMetricsWriter` (`//emulator/libs/metrics:fake_metrics_writer`, header `emulator/libs/metrics/include/goldfish/metrics/testing/fake_metrics_writer.h`) to verify reported `EmulatorDetails` events.
* **Secure Authentication:** `TelnetAuth` ([telnet_auth.cc](telnet_auth.cc)) manages the telnet security token (typically stored in `~/.emulator_console_auth_token`).


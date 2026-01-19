# Component: Modem Simulator

**Role:** Simulates a cellular modem (RIL backend) for the emulator, handling AT commands and managing simulated SIM, Network, and Call states.
**Location:** `hardware/generic/goldfish/emulator/modem_simulator`
**Namespace:** `cuttlefish` (Note: Originally from Cuttlefish, integrated into Goldfish).

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ModemSimulator` | `:modem_simulator_lib` | `.../modem_simulator.h` | Core simulator instance. |
| `ChannelMonitor` | `:modem_simulator_lib` | `.../channel_monitor.h` | Manages socket connections from the guest RIL. |

## Critical Infrastructure
* **AT Command Parsing:** `CommandParser` and `PduParser` handle the low-level string/hex-based cellular protocol.
* **Services:** The simulator is modularized into several services:
    *   **SimService:** Simulates SIM card states, PINs, and profiles.
    *   **NetworkService:** Simulates registration, signal strength, and operator selection.
    *   **CallService:** Manages simulated voice calls and state transitions.
    *   **SmsService:** Handles SMS send/receive and PDU management.
    *   **DataService:** Simulates data connection (PDP context) setup.
* **NVRAM:** `NvramConfig` persists modem state (e.g., phone number, carrier info) across reboots.
* **Channel Management:** `ChannelMonitor` uses a background thread and `select()` to multiplex multiple RIL channels and remote control connections.

## Dependencies
* **Core:** `@jsoncpp`, `@tinyxml2` (Configuration loading).
* **System:** `//android/system`.

## Threading Model
* **Monitor Thread:** `ChannelMonitor` runs a dedicated thread for non-blocking I/O on communication channels.
* **Looper:** `ThreadLooper` provides an internal event loop for delayed tasks and state machine transitions.
* **Synchronization:** Uses `std::mutex` to protect shared state across services.

## Flows & Guides
* [AT Command Flow](docs/at_command_flow.md)

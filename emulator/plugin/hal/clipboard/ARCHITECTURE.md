# Component: Clipboard HAL

**Role:** Synchronizes the clipboard content between the Host and the Guest.
**Location:** `hardware/generic/goldfish/emulator/hal/clipboard`
**Namespace:** `goldfish::devices::clipboard`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ClipboardDevice` | `:clipboard` | `.../clipboard_device.h` | The HAL implementation. |

## Critical Infrastructure
* **State Management:** Uses `avd_universe::clipboard::ClipboardChannel` to publish/subscribe to clipboard changes.
* **Flow:**
    *   **Host -> Guest:** Subscribes to `host_to_guest` observable. When updated, sends data to guest via `HalSocket`.
    *   **Guest -> Host:** Receives data via `OnReceive`, updates `guest_to_host` observable.

## Dependencies
* **Core:** `//emulator/hal/plug`.
* **State:** `//emulator/libs/avd_universe:clipboard`.

## Threading Model
* **Thread Safe:** Inherits `HalPlug`. State updates are handled via `ObservableValue` (thread-safe).

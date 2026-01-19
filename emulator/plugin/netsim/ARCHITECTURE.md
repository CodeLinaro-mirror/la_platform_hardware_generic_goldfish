# Component: Netsim Plugin

**Role:** Connects QEMU network and character devices to the external `netsimd` process for simulating WiFi, Bluetooth, and UWB.
**Location:** `hardware/generic/goldfish/emulator/plugin/netsim`
**Namespace:** `goldfish::netsim`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `netsim_netdev_register_types` | `:netsim-netdev` | `.../netsim_netdev.h` | Registers `netsim-netdev` (WiFi). |
| `netsim_chardev_register_types` | `:netsim-chardev` | `.../netsim_chardev.h` | Registers `chardev-netsim-bt` and `chardev-netsim-uwb`. |

## Critical Infrastructure
* **NetsimTransport:** Handles the gRPC streaming connection to `netsimd`.
* **WiFi (NetDev):** Implements a `NetClient` (`netsim-netdev`). It receives 802.11 frames from the guest (via `mac80211_hwsim`), wraps them in Protobuf, and sends them to `netsimd`.
* **Bluetooth/UWB (CharDev):** Implements `Chardev` backends (`chardev-netsim-bt`, `chardev-netsim-uwb`).
    *   **BT:** Parses HCI packets (using `H4Parser`), wraps them, and sends to `netsimd`.
    *   **UWB:** Parses UCI packets and forwards them.

## Dependencies
* **External:** `@netsim` (Protobuf definitions, gRPC client).
* **Upstream:** `@qemu` (NetClient, Chardev).

## Threading Model
* **QEMU Context:** `receive`, `chr_write` run on the QEMU main loop (or IO thread).
* **gRPC:** Runs on a background thread pool. Responses from `netsimd` are dispatched back to the QEMU loop via `NetsimTransport` (which likely uses `QemuEventLoop` or similar mechanism, though `next_recv` implies a polling or async read loop).

## Flows & Guides
* [Packet Flow](docs/packet_flow.md)

# Component: Virtio WiFi Plugin

**Role:** Implements the `virtio-wifi` QEMU device, which exposes a simulated Wi-Fi interface to the guest using the `mac80211_hwsim` protocol.
**Location:** `hardware/generic/goldfish/emulator/plugin/virtio-wifi`
**Namespace:** `TYPE_VIRTIO_WIFI` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `virtio_wifi_register_types` | `:virtio-wifi` | `.../virtio_wifi.h` | QEMU type registration. |

## Critical Infrastructure
* **NIC Backend:** Connects to a QEMU `NetClient` (typically `netsim-netdev`, see `plugin/netsim`).
* **Protocol:** Implements the `mac80211_hwsim` over virtio. It exchanges 802.11 frames encapsulated in `mac80211_hwsim` structs with the guest driver.
* **MAC Address:** Generates a MAC address based on the emulator serial number (`mac_prefix`), ensuring unique addresses in multi-instance setups.

## Dependencies
* **Upstream:** `@qemu` (Virtio, Networking).
* **Linux Headers:** `standard-headers/linux/mac80211_hwsim.h`.

## Threading Model
* **QEMU Context:** `virtio_wifi_nic_rx`, `virtio_wifi_tx_bh` run on the QEMU main thread (BQL held).

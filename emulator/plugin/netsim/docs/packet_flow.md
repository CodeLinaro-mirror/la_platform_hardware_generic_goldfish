# Netsim Packet Flow

This document details how 802.11 (WiFi) and HCI (Bluetooth) frames are routed between the Android Guest OS and the `netsimd` process.

## WiFi Packet Flow (NetDev)

The `netsim-netdev` plugin implements a QEMU Network Backend (`NetClient`).

```mermaid
sequenceDiagram
    participant Guest as Guest OS (mac80211_hwsim)
    participant Virtio as Virtio-WiFi
    participant NetDev as NetsimNetDev
    participant Transport as NetsimTransport
    participant Netsimd as Netsimd Process

    Note over Guest, Netsimd: Transmission (Guest -> Netsimd)

    Guest->>Virtio: Transmit 802.11 Frame
    Virtio->>NetDev: netsim_netdev_receive()
    NetDev->>NetDev: Filter (Check HWSIM_CMD_FRAME)
    NetDev->>Transport: send(PacketRequest)
    Transport->>Netsimd: gRPC Stream (Send)

    Note over Guest, Netsimd: Reception (Netsimd -> Guest)

    Netsimd->>Transport: gRPC Stream (Recv)
    Transport->>Transport: Callback(PacketResponse)
    Transport->>NetDev: netsim_netdev_send()
    NetDev->>Virtio: qemu_send_packet_async()
    Virtio->>Guest: Receive Interrupt
```

## Bluetooth/UWB Packet Flow (CharDev)

Bluetooth and UWB use a QEMU Character Device backend (`netsim-chardev`).

```mermaid
sequenceDiagram
    participant Guest as Guest OS (Virtio-Console)
    participant CharDev as NetsimCharDev
    participant Protocol as BtProtocol (H4Parser)
    participant Transport as NetsimTransport
    participant Netsimd as Netsimd Process

    Note over Guest, Netsimd: Transmission (Guest -> Netsimd)

    Guest->>CharDev: Write (HCI Data)
    CharDev->>Protocol: guest_to_netsim_parser_consume()
    Protocol->>Protocol: H4 Parse (Reassemble Packet)
    Protocol-->>CharDev: Packet Complete
    CharDev->>Transport: send(PacketRequest)
    Transport->>Netsimd: gRPC Stream

    Note over Guest, Netsimd: Reception (Netsimd -> Guest)

    Netsimd->>Transport: gRPC Stream
    Transport->>Protocol: netsim_to_guest_packet()
    Protocol->>CharDev: qemu_chr_be_write()
    CharDev->>Guest: Read Data
```

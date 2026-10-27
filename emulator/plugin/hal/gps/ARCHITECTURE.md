# Component: GPS HAL

**Role:** Provides location (GPS) data to the guest. **Location:** `emulator/plugin/hal/gps/`
**Namespace:** `goldfish::devices::gps`

## Integration Guide

| Class / Interface | Bazel Target | Header Path        | Description             |
| :---------------- | :----------- | :----------------- | :---------------------- |
| `IGpsDevice`      | `:gps`       | `.../gps_device.h` | The HAL interface.      |
| `GpsDevice`       | `:gps`       | `gps_device.cc`    | The HAL implementation. |

## Critical Infrastructure

- **Protocol:** Uses the legacy QEMUD GPS protocol (`$GPGGA` and `$GPRMC` NMEA 0183 sentences framed
  as QEMUD packets).
- **State Management:** Subscribes to `avd_universe::gps::ObservableLocation` to receive location
  updates from host controllers (UI, console, gRPC `GpsServiceImpl`).
- **Behavior:** Broadcasts the current location at 1 Hz while connected and immediately transmits an
  update whenever `ObservableLocation` changes.

## Dependencies

- **Core:** `//emulator/plugin/hal/plug`.
- **Connector:** `//emulator/plugin/hal/connector:qemud`.
- **State:** `//emulator/plugin/avd_universe:location`.

## Threading & Event Loop Model

`GpsDevice` executes all `HalPlug` callbacks (`OnConnect`, `OnClose`, `OnReceive`), location
updates, and its 1 Hz timer on `qemu_loop` (`QemuEventLoop`):

- **Virtual Clock Timer (`QEMU_CLOCK_VIRTUAL`):** `one_second_timer_` is a `QemuTimer` created on
  `qemu_loop`, so periodic 1 Hz NMEA broadcasts follow guest virtual time and pause automatically
  when the VM is paused or suspended.
- **Single-Loop Serialization:** `HalPlug` lifecycle callbacks (`OnConnect`, `OnClose`),
  `one_second_timer_`, and `Socket()` access all run on `qemu_loop` (`IsOnLoopThread() == true`).
  `OnClose()` cancels `one_second_timer_` and unsubscribes from `ObservableLocation` synchronously
  on the loop thread.
- **Asynchronous Location Updates:** `ObservableLocation` updates from external threads (UI,
  console, gRPC `GpsServiceImpl`) post `[weak_dev, location]` to `qemu_loop` before locking
  `weak_dev`, keeping all `GpsDevice` state (`location_`, `one_second_timer_`,
  `location_update_subscription_`) serialized on `qemu_loop` without internal locks.

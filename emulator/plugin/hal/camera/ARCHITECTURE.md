# Component: Camera HAL

**Role:** Emulates camera devices, supporting various backends (Webcam, Video File, Virtual Scene).
**Location:** `hardware/generic/goldfish/emulator/hal/camera`
**Namespace:** `goldfish::devices::camera`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `CameraDevice` | `:camera` | `.../camera_device.h` | Main class representing an emulated camera. |
| `CameraImageProvider` | `:image_provider_api` | `.../camera_image_provider_api.h` | Interface for camera backends (webcam, file, etc.). |
| `RegisterCameraDevice` | `:camera` | `.../register_device.h` | Helper to register camera devices with the connector registry. |

## Critical Infrastructure
* **Image Provider Registry:** Allows dynamic registration of camera backends (e.g., `webcam`, `videofile`).
* **Protocol:** Implements the Goldfish Camera protocol to communicate with the guest's `camera.goldfish.so` HAL.
* **Enumeration:** `CameraDeviceEnumerator` scans for available providers and configurations based on `HardwareConfig`.

## Dependencies
* **Core:** `//emulator/hal/connector` (Registration).
* **Imaging:** `//emulator/libs/imaging` (Pixel formats).
* **Config:** `//emulator/config:hardware_config`.
* **Providers:** Includes implementations for `imagefile`, `videofile`, `virtualscene`, `webcam`.

## Threading Model
* **Thread Safe:** Inherits from `HalPlug`. Callbacks are serialized.
* **Image Sources:** Providers typically capture frames on their own threads and push them to the `CameraDevice`.

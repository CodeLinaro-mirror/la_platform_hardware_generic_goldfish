# Component: Display

**Role:** Abstraction layer for emulator displays, handling pixel retrieval and input event injection.
**Location:** `hardware/generic/goldfish/emulator/libs/display`
**Namespace:** `goldfish::display`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `IDisplay` | `@goldfish//emulator/libs/display:base` | `.../display/display.h` | Abstract base class for a display. |
| `PixmanDisplay` | `@goldfish//emulator/libs/display` | `.../display/pixman_display.h` | Implementation backed by a `pixman_image_t`. |
| `MultiDisplay` | `@goldfish//emulator/libs/display` | `.../multi_display.h` | Manager for multiple `IDisplay` instances. |

## Critical Infrastructure
* **IDisplay:**
    *   **Events:** Inherits from `FrameInfoCallbackSource` to signal new frames to listeners.
    *   **Synchronization:** Uses `absl::Mutex` to protect the frame sequence number (`mSeq`).
    *   **Input:** Provides methods (`sendMultiTouchEvent`, `sendMouseEvent`) to inject input back into the guest (or virtio bridge).
* **Pixman integration:** Wraps the Cairo/Pixman library for software rendering and format conversion.

## Dependencies
* **Core:** `@abseil-cpp` (Mutex, Status, Time).
* **Goldfish:** `//emulator/libs/base` (UniqueHandle), `//emulator/libs/fps_calculator`, `//android/async`.
* **External:** `@pixman`, `@libpng`.

## Threading Model
* **Thread Safe:** `IDisplay` methods like `seq()` and `waitForFrame()` are thread-safe.
* **Frame Delivery:** Frame updates (`frameReceived`) signal the `FrameInfoCallbackSource` which likely dispatches callbacks on the `EventLoop`.

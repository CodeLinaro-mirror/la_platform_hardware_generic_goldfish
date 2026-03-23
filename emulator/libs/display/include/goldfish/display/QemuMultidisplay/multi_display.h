// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <atomic>
#include <variant>
#include <vector>

#include "goldfish/async/event_loop.h"
#include "goldfish/async/event_loop_dispatcher.h"
#include "goldfish/display/display.h"

namespace goldfish::display {

using ::goldfish::async::EventLoop;
using ::goldfish::async::LoopBoundCallbackSource;

using DisplayId = unsigned;

struct DisplayEvent {
    using AddedEvent = DisplayPtr;
    using DeletedEvent = DisplayId;

    std::variant<AddedEvent, DeletedEvent> event_data;

    // Helper functions to check event type and access data safely
    bool IsAddedEvent() const { return std::get_if<AddedEvent>(&event_data); }
    bool IsDeletedEvent() const { return std::get_if<DeletedEvent>(&event_data); }

    DisplayPtr Display() const {
        if (IsAddedEvent()) {
            return std::get<AddedEvent>(event_data);
        }
        return {};
    }

    DisplayId DisplayId() const {
        if (IsDeletedEvent()) {
            return std::get<DeletedEvent>(event_data);
        }
        return -1;
    }
};

/**
 * @class MultiDisplay
 * @brief Singleton class managing a collection of IDisplay objects.
 *
 */
class IMultiDisplay : public LoopBoundCallbackSource<DisplayEvent> {
  public:
    /**
     * @brief Returns the singleton Instance of MultiDisplay.
     * @return Pointer to the MultiDisplay Instance.
     */
    static IMultiDisplay* Instance();
    static void InjectSingleton(IMultiDisplay* display);

    explicit IMultiDisplay(EventLoop* loop)
            : LoopBoundCallbackSource<DisplayEvent>(loop), loop_(loop) {}
    virtual ~IMultiDisplay() = default;

    /**
     * @brief Creates a new IDisplay object and adds it to the managed collection.
     *
     * @param display_id The unique identifier for the new display.
     * @param width The width of the display in pixels.
     * @param height The height of the display in pixels.
     * @return absl::StatusOr containing a raw IDisplay* pointer to the newly created IDisplay on
     * success, or an error absl::Status on failure. MultiDisplay takes ownership of the created
     * IDisplay object. The client is responsible for checking the absl::StatusOr and handling
     * potential errors. On success, the client receives a raw pointer and MUST NOT delete it.
     *
     * TODO(jansene): This is to be called from the UI to create an addition display in the device.
     */
    virtual absl::StatusOr<DisplayPtr> CreateDisplay(DisplayId display_id, uint32_t width,
                                                     uint32_t height, uint32_t dpi,
                                                     uint32_t flags) = 0;
    /**
     * @brief Returns the enabled state of MultiDisplay.
     * @return True if MultiDisplay is enabled, false otherwise.
     */
    virtual bool IsEnabled() const = 0;

    /**
     * @brief Gets an IDisplay object by its ID.
     *
     * @param display_id The unique identifier of the display to retrieve.
     * @return Raw IDisplay* pointer if the display with the given ID is found, nullptr otherwise.
     */
    virtual absl::StatusOr<DisplayPtr> GetDisplay(DisplayId display_id) const = 0;

    /**
     * @brief Gets an active IDisplay object, potentially redirecting from display 0 to 1
     *        if the device is a foldable and display 0 is inactive.
     *
     * @param display_id The unique identifier of the display to retrieve.
     * @param has_hinge True if the hardware supports a hinge (foldable).
     * @return absl::StatusOr<SharedDisplay> containing the display if found and active.
     */
    absl::StatusOr<SharedDisplay> GetActiveDisplay(DisplayId display_id, bool has_hinge) const;

    /**
     * @brief Erases an IDisplay object from the managed collection and destroys it.
     *
     *  **Important:** This method MUST be called explicitly when a display is no longer needed
     *  to ensure proper cleanup and resource release. Displays are NOT automatically removed
     *  when they go out of scope.
     *
     * @param display_id The unique identifier of the display to erase.
     * @return absl::Status indicating success or failure.
     *         - absl::OkStatus() on success.
     *         - absl::NotFoundError if the display with the given ID is not found.
     *         - absl::InvalidArgumentError if the display ID is invalid (i.e. 0 you cannot delete
     * the default display)
     *         - absl::InternalError on internal errors.
     */
    virtual absl::Status EraseDisplay(DisplayId display_id) = 0;

    /* Snapshot of all the active displays */
    virtual std::vector<DisplayPtr> Displays() const = 0;

    /**
     * @brief Retrieves the default display of the Android device.
     *
     * This method returns the main display of the Android device, which is typically
     * created very early during the device's initialization. The default display
     * is always associated with `display_id == 0`.
     *
     * @return absl::StatusOr<DisplayPtr> An `absl::StatusOr` containing:
     *         - A `DisplayPtr` to the default display on success.
     *         - An error `absl::Status` if the default display (display_id 0) is not found.
     *
     * @note The default display is guaranteed to exist in a properly initialized
     *       Android device. If this method returns an error, it indicates a
     *       critical issue with the display system.
     * @note This is equivalent to calling `GetDisplay(0)`.
     */
    absl::StatusOr<DisplayPtr> DefaultDisplay() const { return GetDisplay(0); }

    static constexpr size_t kMaxDisplays = 11;  ///< Maximum number of supported Android displays.

  protected:
    EventLoop* loop_;

  private:
    static std::atomic<IMultiDisplay*> g_singleton;
};

namespace qemu_multidisplay {
void ConfigureMultiDisplay(EventLoop* loop, EventLoop* qemu_loop);
}

}  // namespace goldfish::display

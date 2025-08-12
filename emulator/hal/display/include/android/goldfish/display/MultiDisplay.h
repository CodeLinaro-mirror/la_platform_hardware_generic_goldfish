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

#include <vector>

#include "android/goldfish/display/Display.h"

namespace android::goldfish {

using DisplayId = unsigned;

struct DisplayEvent {
    using AddedEvent = DisplayPtr;
    using DeletedEvent = DisplayId;

    std::variant<AddedEvent, DeletedEvent> eventData;

    // Helper functions to check event type and access data safely
    bool isAddedEvent() const { return std::get_if<AddedEvent>(&eventData); }
    bool isDeletedEvent() const { return std::get_if<DeletedEvent>(&eventData); }

    DisplayPtr display() const {
        if (isAddedEvent()) {
            return std::get<AddedEvent>(eventData);
        }
        return {};
    }

    DisplayId displayId() const {
        if (isDeletedEvent()) {
            return std::get<DeletedEvent>(eventData);
        }
        return -1;
    }
};

/**
 * @class MultiDisplay
 * @brief Singleton class managing a collection of IDisplay objects.
 *
 */
class IMultiDisplay : public WithCallbacks<EventChangeSupport, DisplayEvent> {
  public:
    /**
     * @brief Returns the singleton instance of MultiDisplay.
     * @return Pointer to the MultiDisplay instance.
     */
    static IMultiDisplay* instance();

    /**
     * @brief Creates a new IDisplay object and adds it to the managed collection.
     *
     * @param displayId The unique identifier for the new display.
     * @param width The width of the display in pixels.
     * @param height The height of the display in pixels.
     * @return absl::StatusOr containing a raw IDisplay* pointer to the newly created IDisplay on
     * success, or an error absl::Status on failure. MultiDisplay takes ownership of the created
     * IDisplay object. The client is responsible for checking the absl::StatusOr and handling
     * potential errors. On success, the client receives a raw pointer and MUST NOT delete it.
     *
     * TODO(jansene): This is to be called from the UI to create an addition display in the device.
     */
    virtual absl::StatusOr<DisplayPtr> createDisplay(DisplayId displayId, uint32_t width,
                                                     uint32_t height) = 0;

    /**
     * @brief Returns the enabled state of MultiDisplay.
     * @return True if MultiDisplay is enabled, false otherwise.
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Gets an IDisplay object by its ID.
     *
     * @param displayId The unique identifier of the display to retrieve.
     * @return Raw IDisplay* pointer if the display with the given ID is found, nullptr otherwise.
     */
    virtual absl::StatusOr<DisplayPtr> getDisplay(DisplayId displayId) const = 0;

    /**
     * @brief Erases an IDisplay object from the managed collection and destroys it.
     *
     *  **Important:** This method MUST be called explicitly when a display is no longer needed
     *  to ensure proper cleanup and resource release. Displays are NOT automatically removed
     *  when they go out of scope.
     *
     * @param displayId The unique identifier of the display to erase.
     * @return absl::Status indicating success or failure.
     *         - absl::OkStatus() on success.
     *         - absl::NotFoundError if the display with the given ID is not found.
     *         - absl::InvalidArgumentError if the display ID is invalid (i.e. 0 you cannot delete
     * the default display)
     *         - absl::InternalError on internal errors.
     */
    virtual absl::Status eraseDisplay(DisplayId displayId) = 0;

    /* Snapshot of all the active displays */
    virtual std::vector<DisplayPtr> displays() const = 0;

    /**
     * @brief Retrieves the default display of the Android device.
     *
     * This method returns the main display of the Android device, which is typically
     * created very early during the device's initialization. The default display
     * is always associated with `displayId == 0`.
     *
     * @return absl::StatusOr<DisplayPtr> An `absl::StatusOr` containing:
     *         - A `DisplayPtr` to the default display on success.
     *         - An error `absl::Status` if the default display (displayId 0) is not found.
     *
     * @note The default display is guaranteed to exist in a properly initialized
     *       Android device. If this method returns an error, it indicates a
     *       critical issue with the display system.
     * @note This is equivalent to calling `getDisplay(0)`.
     */
    absl::StatusOr<DisplayPtr> defaultDisplay() const { return getDisplay(0); }

    static constexpr size_t maxDisplays = 11;  ///< Maximum number of supported Android displays.
};

}  // namespace android::goldfish
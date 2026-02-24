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

#include "goldfish/display/QemuMultidisplay/multi_display.h"

namespace goldfish::display::test {

/**
 * @brief A fake implementation of the IMultiDisplay interface.
 *
 * This class is used for testing purposes and simulates a MultiDisplay.
 * It creates and manages ActiveFakePixmanDisplay objects.
 */
class FakeMultiDisplay : public IMultiDisplay {
  public:
    explicit FakeMultiDisplay(EventLoop* loop);
    ~FakeMultiDisplay() override = default;

    /**
     * @brief Returns the singleton Instance of FakeMultiDisplay.
     *
     * @return A pointer to the FakeMultiDisplay Instance.
     */
    static IMultiDisplay* Instance();

    /**
     * @brief Creates a new ActiveFakePixmanDisplay object and adds it to the managed collection.
     *
     * @param display_id The unique identifier for the new display.
     * @param width The width of the display in pixels.
     * @param height The height of the display in pixels.
     * @return absl::StatusOr containing a DisplayPtr to the newly created
     *         ActiveFakePixmanDisplay on success, or an error absl::Status on failure.
     */
    absl::StatusOr<DisplayPtr> CreateDisplay(DisplayId display_id, uint32_t width, uint32_t height,
                                             uint32_t dpi, uint32_t flags) override;

    /**
     * @brief Returns the enabled state of FakeMultiDisplay.
     *
     * @return True if FakeMultiDisplay is enabled, false otherwise.
     */
    bool IsEnabled() const override;

    /**
     * @brief Gets an ActiveFakePixmanDisplay object by its ID.
     *
     * @param display_id The unique identifier of the display to retrieve.
     * @return absl::StatusOr containing a DisplayPtr to the ActiveFakePixmanDisplay if found,
     *         or an error absl::Status if not found.
     */
    absl::StatusOr<DisplayPtr> GetDisplay(DisplayId display_id) const override;

    /**
     * @brief Erases an ActiveFakePixmanDisplay object from the managed collection and destroys it.
     *
     * @param display_id The unique identifier of the display to erase.
     * @return absl::Status indicating success or failure.
     */
    absl::Status EraseDisplay(DisplayId display_id) override;

    /**
     * @brief Returns a snapshot of all the active displays.
     *
     * @return A vector of DisplayPtr to the active displays.
     */
    std::vector<DisplayPtr> Displays() const override;

    template <typename T>
    std::shared_ptr<T> GetDisplay(absl::StatusOr<DisplayPtr> status) {
        return std::static_pointer_cast<T>(status.value().lock());
    }
    /**
     * @brief Clears all displays, accept display 0.
     */
    void Clear();

  private:
    std::unordered_map<uint8_t, SharedDisplay> displays_;
    static FakeMultiDisplay* s_instance;
    bool enabled_{true};
};

}  // namespace goldfish::display::test

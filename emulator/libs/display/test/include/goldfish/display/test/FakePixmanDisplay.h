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
// limitations under the License.D
#pragma once

#include <memory>

#include "absl/synchronization/mutex.h"

#include "PixmanImageGenerator.h"
#include "aemu/base/events/EventSources.h"
#include "goldfish/display/PixmanDisplay.h"

extern "C" {
// clang-format off
  // IWYU pragma: begin_keep
  #include "qemu/osdep.h"
  #include "pixman.h"
  // IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display::test {

/**
 * @brief Represents a fake evdev event.
 *
 * This struct is used to store information about a simulated evdev event.
 */
struct FakeEvDevEvent {
    /**
     * @brief The type of the evdev event.
     */
    uint16_t type;
    /**
     * @brief The code of the evdev event.
     */
    uint16_t code;
    /**
     * @brief The value of the evdev event.
     */
    uint32_t value;

    /**
     * @brief Checks if two FakeEvDevEvent objects are equal.
     *
     * @param other The other FakeEvDevEvent object to compare with.
     * @return True if the two objects are equal, false otherwise.
     */
    bool operator==(const FakeEvDevEvent& other) const {
        return type == other.type && code == other.code && value == other.value;
    }
};

/**
 * @brief Represents a fake mouse event.
 *
 * This struct is used to store information about a simulated mouse event.
 */
struct FakeMouseEvent {
    /**
     * @brief The x-coordinate of the mouse event.
     */
    int x;
    /**
     * @brief The y-coordinate of the mouse event.
     */
    int y;
    /**
     * @brief The button mask of the mouse event.
     */
    int button_mask;
};

/**
 * @brief Represents a fake multi-touch event.
 *
 * This struct is used to store information about a simulated multi-touch event.
 */
struct FakeMultiTouchEvent {
    /**
     * @brief The slot of the multi-touch event.
     */
    uint8_t slot;
    /**
     * @brief The x-coordinate of the multi-touch event.
     */
    int x;
    /**
     * @brief The y-coordinate of the multi-touch event.
     */
    int y;
    /**
     * @brief The type of the multi-touch event.
     */
    MultiTouchType type;
};

/**
 * @brief A fake implementation of the PixmanDisplay class.
 *
 * This class is used for testing purposes and simulates a PixmanDisplay.
 */
struct FakePixmanDisplay : public PixmanDisplay {
  public:
    /**
     * @brief Constructs a FakePixmanDisplay.
     *
     * @param id The ID of the display.
     * @param image The initial pixman image to display.
     */
    FakePixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image);
    FakePixmanDisplay(EventLoop* loop, int id, PixmanImagePtr image);

    /**
     * @brief Destroys the FakePixmanDisplay.
     */
    ~FakePixmanDisplay() = default;

    /**
     * @brief Sends a simulated multi-touch event.
     *
     * @param slot The slot of the multi-touch event.
     * @param x The x-coordinate of the multi-touch event.
     * @param y The y-coordinate of the multi-touch event.
     * @param type The type of the multi-touch event.
     */
    void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override;
    /**
     * @brief Sends a simulated mouse event.
     *
     * @param x The x-coordinate of the mouse event.
     * @param y The y-coordinate of the mouse event.
     * @param button_mask The button mask of the mouse event.
     */
    void sendMouseEvent(int x, int y, int button_mask) override;
    /**
     * @brief Sends a simulated evdev event.
     *
     * @param type The type of the evdev event.
     * @param code The code of the evdev event.
     * @param value The value of the evdev event.
     */
    void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override;

    /**
     * @brief Updates the source image of the display, triggering a new frame event
     *
     * @param image The new pixman image to display.
     */
    void updateSourceImage(::pixman_image_t* image) override;

    /**
     * @brief Gets the current source image of the display.
     *
     * @return A pointer to the current pixman image.
     */
    PixmanImagePtr image() { return mFrameManager->getRenderableImage(); }

    std::vector<FakeEvDevEvent> mEvdevs;       ///< A list of simulated evdev events.
    std::vector<FakeMouseEvent> mMouseEvents;  ///< A list of simulated mouse events.
    std::vector<FakeMultiTouchEvent>
            mMultiTouchEvents;  ///< A list of simulated multi-touch events.
};

/**
 * @brief A FakePixmanDisplay that actively generates images using a PixmanImageGenerator.
 *
 * This class combines a FakePixmanDisplay with a PixmanImageGenerator to create a display that
 * continuously updates its image.
 */
class ActiveFakePixmanDisplay : public FakePixmanDisplay,
                                public android::base::eventing::EventListener<PixmanImagePtr> {
 public:
  /**
   * @brief Destroys the ActiveFakePixmanDisplay.
   */
  ~ActiveFakePixmanDisplay() override;

  /**
   * @brief Called when a new image is generated by the PixmanImageGenerator.
   *
   * @param image The newly generated pixman image.
   */
  void eventArrived(const PixmanImagePtr& image) override;

  /**
   * @brief Starts the image generation process.
   */
  void start();

  /**
   * @brief Stops the image generation process.
   */
  void stop();

  /**
   * @brief Resizes the generated images.
   *
   * @param w The new width.
   * @param h The new height.
   */
  void resize(int w, int h);

  /**
   * @brief Waits for a specific number of frames to be generated with a timeout.
   *
   * @param n The number of frames to wait for.
   * @param timeout The maximum time to wait.
   * @return True if the desired number of frames were generated within the timeout, false
   * otherwise.
   */
  bool waitForFramesWithTimeout(int n, absl::Duration timeout);

  /**
   * @brief Creates a shared_ptr of ActiveFakePixmanDisplay.
   *
   * @param id The ID of the display.
   * @param fps The frames per second at which to generate images.
   * @param w The width of the generated images.
   * @param h The height of the generated images.
   * @return An ActiveFakePixmanDisplay object.
   */
  static std::shared_ptr<ActiveFakePixmanDisplay> createShared(EventLoop* loop, int id, int fps,
                                                               int w, int h);

 private:
  /**
   * @brief Constructs an ActiveFakePixmanDisplay.
   *
   * @param id The ID of the display.
   * @param generator A unique pointer to the PixmanImageGenerator.
   */
  ActiveFakePixmanDisplay(EventLoop* loop, int id, std::unique_ptr<PixmanImageGenerator> generator);

  std::unique_ptr<PixmanImageGenerator> mGenerator;
};
}  // namespace goldfish::display::test

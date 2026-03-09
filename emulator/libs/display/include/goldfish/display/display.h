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

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/async/event_loop_dispatcher.h"

namespace goldfish::display {

using ::goldfish::async::EventLoop;
using ::goldfish::async::LoopBoundCallbackSource;

/**
 * @struct FrameInfo
 * @brief  Represents information about a display frame update.
 *
 *  This struct contains the frame sequence number and a timestamp
 *  indicating when the frame was updated. This allows clients
 *  to not only track frame updates but also have temporal context.
 */
struct FrameInfo {
    uint64_t sequence_number;  ///< Monotonically increasing frame sequence number.
    absl::Time timestamp;     ///< Timestamp when the frame was updated.

    explicit FrameInfo(uint64_t seq) : sequence_number(seq), timestamp(absl::Now()) {}
};

struct Dimensions {
    uint32_t width;
    uint32_t height;
};

struct ResizeEvent {
    uint8_t display_id;
    uint32_t previous_width;
    uint32_t previous_height;
    uint32_t width;
    uint32_t height;
};

enum class MultiTouchType : std::uint8_t {
    kBegin,   //  = INPUT_MULTI_TOUCH_TYPE_BEGIN,
    kUpdate,  // = INPUT_MULTI_TOUCH_TYPE_UPDATE,
    kEnd,     // = INPUT_MULTI_TOUCH_TYPE_END,
    kCancel,  // = INPUT_MULTI_TOUCH_TYPE_CANCEL,
    kData,    // = INPUT_MULTI_TOUCH_TYPE_DATA,
};

enum class PixelFormat : std::uint8_t {
    // Portable Network Graphics format
    // (https://en.wikipedia.org/wiki/Portable_Network_Graphics)
    kPng = 0,

    // Three-channel RGB color model supplemented with a fourth alpha
    // channel. https://en.wikipedia.org/wiki/RGBA_color_model
    // Each pixel consists of 4 bytes.
    kRgba8888,

    // Three-channel RGB color model, each pixel consists of 3 bytes
    kRgb888,
};

enum class Orientation : std::uint8_t {
    kPortrait,
    kLandscape,
    kSquare,
};

/**
 * @enum ImageRotation
 * @brief Represents the supported counter-clockwise rotation angles for pixel
 *        retrieval.
 *
 * Restricting rotations to these four orthogonal values ensures that
 * coordinate transformations (swapping width/height and mirroring axes)
 * are precise and do not introduce interpolation artifacts or require
 * complex arbitrary-angle rotation math.
 */
enum class ImageRotation : std::uint16_t {
    kRotation0 = 0,
    kRotation90 = 90,
    kRotation180 = 180,
    kRotation270 = 270,
};

class IDisplay;
using DisplayPtr = std::weak_ptr<IDisplay>;
using SharedDisplay = std::shared_ptr<IDisplay>;

using FrameInfoCallbackSource = LoopBoundCallbackSource<FrameInfo>;
using ResizeEventCallbackSource = LoopBoundCallbackSource<ResizeEvent>;
/**
 * @class IDisplay
 * @brief Interface representing an Android display.
 *
 *  This interface provides methods to get display properties,
 *  retrieve pixel data in different ways, send virtio input events,
 *  and signal frame updates via events.
 *
 *  It inherits from CallbackEventSource< FrameInfo> to provide
 *  event signaling capabilities. Subscribers can register to receive events
 *  of type FrameInfo, which represents information about a display frame update.
 */
class IDisplay : public FrameInfoCallbackSource,
                 public ResizeEventCallbackSource,
                 public std::enable_shared_from_this<IDisplay> {
  public:
    explicit IDisplay(EventLoop* loop) : IDisplay(loop, 0, 0, 0) {}
    virtual ~IDisplay() = default;

    /**
     * @brief Returns the orientation of the given dimensions.
     * @param w Width
     * @param h Height
     * @return The Orientation (Portrait, Landscape, or Square).
     */
    static Orientation GetOrientation(int w, int h);

    /**
     * @brief Returns the unique identifier of this display.
     * @return The display ID (uint8_t).
     */
    uint8_t Id() const { return display_id_; }

    /**
     * @brief Returns the dimensions of the display.
     * @return The display dimensions (Dimensions).
     */
    Dimensions GetDimensions() const {
        const absl::MutexLock lock(dimension_mutex_);
        return dimensions_;
    }

    uint32_t Dpi() const { return dpi_; }

    uint32_t Flags() const { return flags_; }

    /**
     * Calculates new dimensions to fit a box while preserving aspect ratio.
     * The box dimensions (desired_width, desired_height) are logical dimensions,
     * which means they can be rotated relative to the physical display dimensions.
     * The returned dimensions will match the orientation of the requested box.
     *
     * If either desired_width or desired_height is 0, the function returns {0, 0}.
     * The returned dimensions will never exceed the dimensions of the display.
     *
     * @param desired_width The maximum logical width of the bounding box.
     * @param desired_height The maximum logical height of the bounding box.
     * @return A std::pair<int, int> containing the new logical width and height.
     */
    virtual std::pair<int, int> ResizeKeepAspectRatio(int desired_width, int desired_height);

    /**
     * @brief Returns the current frame information.
     *
     *  This is a monotonically increasing counter that is incremented whenever
     *  the Android display produces a new frame, along with a timestamp of the update.
     *  It can be used to detect when the display content has updated and the timing.
     *
     *  A new frame event with this frame information is also fired via the
     *  WithCallbacks interface whenever a new frame is rendered.
     *  Subscribers can listen for these events to be notified of frame updates and their
     * timestamps.
     *
     * @return The frame information (FrameInfo).
     */
    FrameInfo Seq() const {
        const absl::MutexLock lock(seq_access_);
        return seq_;
    }

    // True if a frame there is a frame that is newer than last_sequence_number before timneout.
    bool WaitForFrame(absl::Duration timeout, uint64_t last_sequence_number) const {
        auto next_frame = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(seq_access_) {
            return seq_.sequence_number > last_sequence_number;
        };
        const absl::MutexLock lock(seq_access_);
        seq_access_.AwaitWithTimeout(absl::Condition(&next_frame), timeout);
        return seq_.sequence_number > last_sequence_number;
    }

    // True if a frame arrived before timneout.
    bool WaitForNextFrame(absl::Duration timeout) const {
        return WaitForFrame(timeout, Seq().sequence_number);
    }

    /**
     * @brief Retrieves pixel data for a specified region, writing it into a caller-provided buffer.
     *
     *  This method allows for reusing a pre-allocated buffer to potentially minimize
     *  memory allocation overhead and copying, especially when called repeatedly.
     *
     * @param format The desired image format (PixelFormat::kRgba8888 or PixelFormat::kRgb888).
     * @param startX The starting X coordinate of the region (inclusive).
     * @param startY The starting Y coordinate of the region (inclusive).
     * @param width The width of the region to retrieve.
     * @param height The height of the region to retrieve.
     * @param pixelBuffer A reference to a std::vector<uint8_t> that will be populated
     *                    with the pixel data. The caller should ensure this vector
     *                    is aDt least large enough to hold the expected pixel data.
     * @return absl::Status indicating success or failure.
     *         absl::OkStatus() on success, or an error absl::Status on failure.
     *         Assumes pixel data is tightly packed in memory (stride == width * bytes_per_pixel).
     *
     * @throws std::invalid_argument If an unsupported PixelFormat is provided (e.g.,
     * PixelFormat::kPng).
     *
     * @note The pixelBuffer vector will be resized to the exact size of the retrieved pixel data.
     *       Only PixelFormat::kRgba8888 and PixelFormat::kRgb888 formats are supported by
     * GetPixels. For kPng format or image resizing/rotation, use higher-level image processing
     * libraries after retrieving raw pixels if needed.
     */
    virtual absl::StatusOr<FrameInfo> GetPixels(PixelFormat fmt, int width, int height,
                                                ImageRotation rotation, uint8_t* pixel,
                                                size_t* c_pixels) const = 0;

    /**
     * Sends a touch event to the proper display
     */
    virtual void SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) = 0;

    /**
     * Sends a mouse event to the proper display
     */
    virtual void SendMouseEvent(int x, int y, int button_mask) = 0;

    /**
     * Sends a raw evdev event to the proper display.
     */
    virtual void SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) = 0;

    // True if it is active (i.e. connected)
    virtual bool Active() const { return active_; }

    /**
     * @brief Sets the active status of the display.
     * @param active True to activate, false to deactivate.
     */
    virtual void SetActive(bool active) { active_ = active; }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const IDisplay* display) {
        absl::Format(&sink, "%s", display ? "<none>" : display->String());
    }

    static SharedDisplay GetNullDisplay();

  protected:
    void SetDimensions(Dimensions dim) {
        const absl::MutexLock lock(dimension_mutex_);
        dimensions_ = dim;
    }

    void SetDimensions(uint32_t width, uint32_t height) {
        SetDimensions({.width = width, .height = height});
    }

    struct LogicalFit {
        int width;
        int height;
        bool swapped;
    };

    /**
     * @brief Calculates the ideal logical dimensions for the display to fit
     *        within the given box while preserving aspect ratio and matching
     *        the box orientation.
     *
     * @param desired_width The maximum logical width of the bounding box.
     * @param desired_height The maximum logical height of the bounding box.
     * @return A LogicalFit struct containing the new logical dimensions and
     *         whether the source was swapped.
     */
    LogicalFit CalculateLogicalFit(int desired_width, int desired_height) const;

    void FrameReceived() {
        const absl::MutexLock lock(seq_access_);
        seq_ = FrameInfo(seq_.sequence_number + 1);
        FrameInfoCallbackSource::FireEvent(seq_);
    }

    virtual std::string String() const;

    IDisplay(EventLoop* loop, uint8_t id, uint32_t width, uint32_t height)
            : FrameInfoCallbackSource(loop)
            , ResizeEventCallbackSource(loop)
            , display_id_(id)
            , dimensions_({.width = width, .height = height}) {}

    FrameInfo seq_ ABSL_GUARDED_BY(seq_access_){0};
    mutable absl::Mutex seq_access_;

    uint32_t dpi_{0};
    uint32_t flags_{0};
    const uint8_t display_id_;
    bool active_{true};

  private:
    Dimensions dimensions_ ABSL_GUARDED_BY(dimension_mutex_);
    mutable absl::Mutex dimension_mutex_;
};

}  // namespace goldfish::display

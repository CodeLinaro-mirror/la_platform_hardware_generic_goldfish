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

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "aemu/base/events/CallbackEventSupport.h"

namespace android::goldfish {

using android::base::EventChangeSupport;
using android::base::WithCallbacks;

struct pixman_image_t;
struct InputEvent;

/**
 * @struct FrameInfo
 * @brief  Represents information about a display frame update.
 *
 *  This struct contains the frame sequence number and a timestamp
 *  indicating when the frame was updated. This allows clients
 *  to not only track frame updates but also have temporal context.
 */
struct FrameInfo {
    uint64_t sequenceNumber;  ///< Monotonically increasing frame sequence number.
    absl::Time timestamp;     ///< Timestamp when the frame was updated.

    FrameInfo(uint64_t seq) : sequenceNumber(seq), timestamp(absl::Now()) {}
};

struct ResizeEvent {
    uint8_t displayId;
    uint32_t previousWidth;
    uint32_t previousHeight;
    uint32_t width;
    uint32_t height;
};

enum class MultiTouchType {
    BEGIN,   //  = INPUT_MULTI_TOUCH_TYPE_BEGIN,
    UPDATE,  // = INPUT_MULTI_TOUCH_TYPE_UPDATE,
    END,     // = INPUT_MULTI_TOUCH_TYPE_END,
    CANCEL,  // = INPUT_MULTI_TOUCH_TYPE_CANCEL,
    DATA,    // = INPUT_MULTI_TOUCH_TYPE_DATA,
};

enum class PixelFormat {
    // Portable Network Graphics format
    // (https://en.wikipedia.org/wiki/Portable_Network_Graphics)
    PNG = 0,

    // Three-channel RGB color model supplemented with a fourth alpha
    // channel. https://en.wikipedia.org/wiki/RGBA_color_model
    // Each pixel consists of 4 bytes.
    RGBA8888,

    // Three-channel RGB color model, each pixel consists of 3 bytes
    RGB888,
};

class IDisplay;
using DisplayPtr = std::weak_ptr<IDisplay>;
using SharedDisplay = std::shared_ptr<IDisplay>;

/**
 * @class IDisplay
 * @brief Interface representing an Android display.
 *
 *  This interface provides methods to get display properties,
 *  retrieve pixel data in different ways, send virtio input events,
 *  and signal frame updates via events.
 *
 *  It inherits from WithCallbacks<EventChangeSupport, FrameInfo> to provide
 *  event signaling capabilities. Subscribers can register to receive events
 *  of type FrameInfo, which represents information about a display frame update.
 */
class IDisplay : public WithCallbacks<EventChangeSupport, FrameInfo>,
                 public WithCallbacks<EventChangeSupport, ResizeEvent> {
  public:
    virtual ~IDisplay() = default;

    /**
     * @brief Returns the unique identifier of this display.
     * @return The display ID (uint8_t).
     */
    uint8_t id() const { return mDisplayId; }

    /**
     * @brief Returns the width of the display in pixels.
     * @return The display width (uint32_t).
     */
    uint32_t width() const { return mWidth; }

    /**
     * @brief Returns the height of the display in pixels.
     * @return The display height (uint32_t).
     */
    uint32_t height() const { return mHeight; }

    uint32_t dpi() const { return mDpi; }

    uint32_t flags() const { return mFlags; }

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
    FrameInfo seq() const {
        absl::MutexLock lock(&mSeqAccess);
        return mSeq;
    }

    // True if a frame there is a frame that is newer than lastSequenceNumber before timneout.
    bool waitForFrame(absl::Duration timeout, int lastSequenceNumber) const {
        auto nextFrame = [&]() { return mSeq.sequenceNumber > lastSequenceNumber; };
        absl::MutexLock lock(&mSeqAccess);
        mSeqAccess.AwaitWithTimeout(absl::Condition(&nextFrame), timeout);
        return mSeq.sequenceNumber > lastSequenceNumber;
    }

    // True if a frame arrived before timneout.
    bool waitForNextFrame(absl::Duration timeout) const {
        return waitForFrame(timeout, seq().sequenceNumber);
    }

    /**
     * @brief Retrieves pixel data for a specified region, writing it into a caller-provided buffer.
     *
     *  This method allows for reusing a pre-allocated buffer to potentially minimize
     *  memory allocation overhead and copying, especially when called repeatedly.
     *
     * @param format The desired image format (ImgFormat::RGBA8888 or ImgFormat::RGB888).
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
     * @throws std::invalid_argument If an unsupported ImgFormat is provided (e.g., ImgFormat::PNG).
     *
     * @note The pixelBuffer vector will be resized to the exact size of the retrieved pixel data.
     *       Only ImgFormat::RGBA8888 and ImgFormat::RGB888 formats are supported by getPixels.
     *       For PNG format or image resizing/rotation, use higher-level
     *       image processing libraries after retrieving raw pixels if needed.
     */
    virtual absl::StatusOr<FrameInfo> getPixels(PixelFormat fmt, int width, int height,
                                                int rotationDeg, uint8_t* pixel,
                                                size_t* cPixels) const = 0;

    /**
     * Sends a touch event to the proper display
     */
    virtual void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) = 0;

    /**
     * Sends a mouse event to the proper display
     */
    virtual void sendMouseEvent(int x, int y, int button_mask) = 0;

    /**
     * Sends a raw evdev event to the proper display.
     */
    virtual void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) = 0;

    // True if it is active (i.e. connected)
    virtual bool active() const { return mActive; }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const IDisplay* display) {
        absl::Format(&sink, "%s", display ? "<none>" : display->string());
    }

    static SharedDisplay nullDisplay();

  protected:
    void frameReceived() {
        absl::MutexLock lock(&mSeqAccess);
        mSeq = FrameInfo(mSeq.sequenceNumber + 1);
        EventChangeSupport<FrameInfo>::fireEvent(mSeq);
    }

    virtual std::string string() const {
        return absl::StrFormat("Display: %d (%dx%d)", mDisplayId, mWidth, mHeight);
    };

    IDisplay(uint8_t id, uint32_t width, uint32_t height)
            : mDisplayId(id), mWidth(width), mHeight(height), mSeq(0) {}

    uint8_t mDisplayId;
    uint32_t mWidth;
    uint32_t mHeight;

    FrameInfo mSeq ABSL_GUARDED_BY(mSeqAccess);
    mutable absl::Mutex mSeqAccess;

    uint32_t mDpi{0};
    uint32_t mFlags{0};
    bool mActive{true};
};

}  // namespace android::goldfish
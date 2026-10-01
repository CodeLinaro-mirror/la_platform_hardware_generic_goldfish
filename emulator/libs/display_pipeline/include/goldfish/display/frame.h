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

// Core frame contracts, read descriptors, slot leases, and producer/sink
// interfaces shared across the display pipeline.
//
// Consumers describe desired pixel transformations with a ReadRequest and a
// caller-owned ReadTarget buffer (or allocator callback). Slot::ReadInto
// validates the request against the slot's native dimensions and delegates the
// resolved ReadRequest to the storage backend's DoReadInto hook in pipeline
// order:
//   crop -> rotate -> scale -> color-space convert
// Producers capture guest frames into pooled Slots, wrap them in move-only
// SlotLeases, and publish them to the display's IFrameSink.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

#include "goldfish/display/geometry.h"

namespace goldfish::display {

/**
 * Monotonically increasing sequence number identifying a published frame within
 * a single display instance. `0` indicates "no frame".
 */
using FrameId = uint64_t;

/**
 * Lightweight notification payload announcing that a display's frame state has
 * advanced.
 *
 * `FrameEvent` carries only metadata (never pixel buffers) so it can be copied
 * and dispatched on the display event loop without blocking or allocating.
 * Consumers receive `FrameEvent` on the display loop and pull the actual
 * `SurfaceFrame` on their own worker thread via `AcquireLatestFrame()`.
 */
struct FrameEvent {
    /** Timestamp when the producer initiated the frame capture (`InfinitePast` if blank). */
    absl::Time capture_time = absl::InfinitePast();
    /** Strictly increasing per-display sequence number; never reused on the same display. */
    FrameId frame_id = 0;
    /** Native (unrotated) frame dimensions; `{0, 0}` only on a removed display's final event. */
    Dimensions dimensions{0, 0};
    /** Counter-clockwise display rotation at the time this event was emitted. */
    ImageRotation rotation = ImageRotation::kRotation0;
    /** Identifier of the display that published this event. */
    uint8_t display_id = 0;
    /** True if this frame has no backing slot (producer reset or no guest content) and reads as
     * black. */
    bool blank = false;

    bool HasGeometryChangeFrom(const FrameEvent& other) const {
        return dimensions != other.dimensions || rotation != other.rotation;
    }
    /** Ordering is meaningful only between events from the same display instance. */
    friend std::strong_ordering operator<=>(const FrameEvent& a, const FrameEvent& b) {
        return a.frame_id <=> b.frame_id;
    }
    friend bool operator==(const FrameEvent& a, const FrameEvent& b) {
        return a.frame_id == b.frame_id;
    }
};

/**
 * Pixel format of a consumer read destination.
 */
enum class ReadFormat : uint8_t { kRgba8888, kRgb888, kNv12 };

/**
 * Color-space conversion matrix used when producing YUV (`kNv12`) output.
 */
enum class YuvMatrix : uint8_t { kBt601Limited };

/**
 * View of a single destination image plane owned by the caller.
 */
struct Plane {
    absl::Span<uint8_t> data;
    /** Row stride in bytes. `0` indicates tightly packed rows (`width * bytes_per_pixel`). */
    size_t stride = 0;
};

/**
 * Destination buffer for 32-bit `R, G, B, A` output in memory order (4 bytes
 * per pixel). The alpha byte is always written as `0xFF` (opaque).
 */
struct Rgba8888Target {
    Plane plane;
};

/**
 * Destination buffer for 24-bit `R, G, B` output in memory order (3 bytes per
 * pixel).
 */
struct Rgb888Target {
    Plane plane;
};

/**
 * Destination buffer for 2-plane 8-bit 4:2:0 `NV12` output: a full-resolution
 * luma plane (`y`) followed by a half-width, half-height interleaved `U, V`
 * chroma plane (`uv`).
 */
struct Nv12Target {
    Plane y;
    Plane uv;
};

/**
 * Type-safe union of caller-owned destination buffers. Encoding the output
 * format in the variant alternative makes it impossible for a caller's format
 * enum and plane count to disagree.
 */
using ReadTarget = std::variant<Rgba8888Target, Rgb888Target, Nv12Target>;

/**
 * Maps a concrete target type (`Rgba8888Target`, `Rgb888Target`, `Nv12Target`)
 * to its corresponding `ReadFormat` at compile time.
 */
template <typename Target>
consteval ReadFormat FormatForTarget() {
    if constexpr (std::is_same_v<Target, Rgba8888Target>) {
        return ReadFormat::kRgba8888;
    } else if constexpr (std::is_same_v<Target, Rgb888Target>) {
        return ReadFormat::kRgb888;
    } else if constexpr (std::is_same_v<Target, Nv12Target>) {
        return ReadFormat::kNv12;
    } else {
        static_assert(sizeof(Target) == 0,
                      "Target must be Rgba8888Target, Rgb888Target, or Nv12Target");
    }
}

/**
 * Axis-aligned rectangle in native (unrotated) frame pixel coordinates.
 */
struct Rect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    friend bool operator==(const Rect&, const Rect&) = default;
};

/**
 * Consumer specification for reading pixels out of a `SurfaceFrame` or `Slot`.
 *
 * Operations are applied in field declaration order:
 *   1. `crop` in native (unrotated) coordinates.
 *   2. `rotation` counter-clockwise (`0`, `90`, `180`, or `270` degrees).
 *   3. `output` downscaling in the rotated coordinate space (upscaling is
 *      rejected with `INVALID_ARGUMENT`).
 *   4. Color-space conversion into the `ReadTarget`'s format using `yuv_matrix`
 *      for YUV targets.
 */
struct ReadRequest {
    /** Sub-region of the frame in native pixels before rotation, or `nullopt` for the full frame.
     */
    std::optional<Rect> crop;
    /** Counter-clockwise rotation applied after cropping. */
    ImageRotation rotation = ImageRotation::kRotation0;
    /**
     * Target dimensions in the rotated orientation, or `{0, 0}` for unscaled
     * rotated crop dimensions. Must not exceed the rotated crop dimensions.
     * For `NV12` targets, odd output dimensions are rounded down to the nearest
     * even integer (`>= 2`).
     */
    Dimensions output{0, 0};
    /** YUV color matrix (ignored for RGB targets). */
    YuvMatrix yuv_matrix = YuvMatrix::kBt601Limited;

    friend bool operator==(const ReadRequest&, const ReadRequest&) = default;
};

/**
 * Buffer size and stride requirements for a single destination plane in a
 * `ReadLayout`.
 *
 * When packing multiple planes (such as `NV12`'s `Y` and `UV` planes) into a
 * single contiguous allocation of size `ReadLayout::total_bytes`, plane `i`
 * starts at byte `offset` and occupies `bytes` bytes with a minimum row stride
 * of `stride` bytes.
 */
struct PlaneLayout {
    /** Byte offset of this plane within a single contiguous allocation (`0` for plane 0). */
    size_t offset = 0;
    /** Minimum (tightly packed) row stride in bytes (`width * bytes_per_sample`). */
    size_t stride = 0;
    /** Minimum required size of `Plane::data` in bytes (`stride * rows`) when tightly packed. */
    size_t bytes = 0;
};

/**
 * Pre-computed destination buffer layout and resolved output dimensions for a
 * (`ReadRequest`, `ReadFormat`) pair, passed to `ReadInto()`'s allocator
 * callbacks.
 *
 * ### Why `ReadLayout` Exists
 * `ReadInto()` is a **zero-allocation** API: instead of allocating and
 * returning a new pixel buffer on every read, it writes directly into
 * caller-owned memory (`ReadTarget` spans) such as a gRPC protobuf `string`, a
 * shared-memory (`mmap`) region, or a pooled `webrtc::NV12Buffer`.
 *
 * However, a `ReadRequest` can apply optional transformations that change the
 * output geometry and byte count:
 *   1. `crop` selects a sub-region (or defaults to the full frame).
 *   2. `rotation` by `90°` or `270°` swaps width and height.
 *   3. `output` downscales to caller-requested dimensions (or defaults to the
 *      rotated crop size).
 *   4. `kNv12` rounds odd output dimensions down to the nearest even integer
 *      (`4:2:0` chroma subsampling requires even width and height) and splits
 *      the output into two planes (`Y` at full height, interleaved `UV` at half
 *      height).
 *
 * Because of these transformations, a caller cannot trivially know how many
 * bytes to allocate, how to slice a contiguous buffer into `Y` and `UV` planes,
 * or what final image dimensions to report in metadata before calling
 * `ReadInto()`. `ReadInto()` resolves the request into a `ReadLayout` up front
 * and passes it to the caller's allocator callback so the caller can allocate
 * or slice its destination buffer(s) with the exact post-transform dimensions
 * and byte counts.
 *
 * ### Typical Usage (Typed Target Allocator)
 * ```cpp
 * ReadRequest req{.rotation = frame->rotation(), .output = {width, height}};
 * std::vector<uint8_t> rgba;
 * ASSIGN_OR_RETURN(Dimensions dims,
 *                  frame->ReadInto(req, [&](const ReadLayout& layout)
 *                                               -> absl::StatusOr<Rgba8888Target> {
 *                      rgba.resize(layout.total_bytes);
 *                      return Rgba8888Target{.plane = {.data = absl::MakeSpan(rgba)}};
 *                  }));
 * // Final image dimensions are dims.width x dims.height.
 * ```
 *
 * Note: `planes[i].stride` is the minimum (tightly packed) stride. Callers
 * writing into externally aligned buffers (such as hardware video encoders or
 * `webrtc::NV12Buffer` with padded row strides) may pass any
 * `Plane::stride >= layout.planes[i].stride` as long as `Plane::data` has at
 * least `Plane::stride * rows` bytes.
 */
struct ReadLayout {
    /**
     * Per-plane layout descriptors. Only the first `plane_count` entries are
     * populated (`planes[0]` for `kRgba8888`/`kRgb888`; `planes[0]` = Y and
     * `planes[1]` = UV for `kNv12`).
     */
    std::array<PlaneLayout, 2> planes{};
    /**
     * Total bytes required to store all `plane_count` planes tightly packed in
     * a single contiguous buffer (`0` if the `ReadRequest` is invalid).
     */
    size_t total_bytes = 0;
    /**
     * Final resolved output dimensions (`width`, `height`) after crop,
     * rotation, scaling, and `kNv12` even-dimension rounding (`{0, 0}` if the
     * `ReadRequest` is invalid).
     */
    Dimensions dimensions{0, 0};
    /** Number of active planes in `planes` (`1` for RGB/RGBA, `2` for `kNv12`). */
    int plane_count = 1;
    /** Target pixel format for which this layout was computed. */
    ReadFormat format = ReadFormat::kRgba8888;

    /**
     * Slices a contiguous byte buffer `[data, data + total_bytes)` into a
     * tightly packed `ReadTarget` matching `format` and `planes`.
     *
     * @param data Pointer to at least `total_bytes` writable bytes. If
     *        `data == nullptr` or `total_bytes == 0`, returns a `ReadTarget`
     *        with empty spans.
     */
    ReadTarget TargetFor(uint8_t* data) const;
};

/**
 * Abstract storage handle representing a single captured frame buffer.
 *
 * Architectural Role:
 * `Slot` decouples frame consumers from producer memory backends (such as host
 * RAM in `CpuSlotStorage`). Using the Non-Virtual Interface (NVI) pattern,
 * `Slot::ReadInto` centralizes all overflow-safe geometry validation, default
 * resolution, and plane-bounds checks before invoking the subclass's
 * `DoReadInto` hook.
 *
 * Thread Safety:
 * Once wrapped in a `SlotLease` and published, a `Slot` is immutable and may be
 * read concurrently by multiple consumer worker threads.
 *
 * Ownership & Lifetimes:
 * Allocated by an `ISlotStorage`, managed while idle by a `SlotPool`, and
 * held exclusively via `SlotLease` while published. Because each `Slot` knows
 * how to read itself, leased slots remain readable even after the originating
 * `ISlotStorage` or `SlotPool` has been destroyed.
 */
class Slot {
  public:
    virtual ~Slot() = default;

    /**
     * @return The slot's native (unrotated) pixel dimensions.
     */
    virtual Dimensions size() const = 0;

    /**
     * Validates `request` and `dst` against `size()`, resolves default crop/size
     * and NV12 even-dimension rounding, and executes the backend read pipeline:
     * crop -> rotate -> scale -> color-space convert.
     *
     * @param request Desired crop, rotation, output dimensions, and YUV matrix.
     * @param dst Caller-owned destination planes. Row padding bytes beyond the
     *        active output width are left untouched.
     * @return `OK` on success, `INVALID_ARGUMENT` if `request` or `dst` does
     *         not fit `size()`, or `INTERNAL` on backend failure.
     */
    absl::Status ReadInto(const ReadRequest& request, const ReadTarget& dst) const;

    /**
     * Resolves `request` for `format`, invokes `allocator(layout)` to allocate a
     * contiguous destination buffer of `layout.total_bytes` bytes, reads the
     * transformed pixels into it, and returns the resolved output `Dimensions`.
     *
     * If `request` is invalid for `size()`, returns `INVALID_ARGUMENT` without
     * invoking `allocator`.
     */
    absl::StatusOr<Dimensions> ReadInto(
            const ReadRequest& request, ReadFormat format,
            absl::FunctionRef<absl::StatusOr<uint8_t*>(const ReadLayout& layout)> allocator) const;

    /**
     * Resolves `request` for `Target`'s format (deduced at compile time from
     * `allocator`'s return type `absl::StatusOr<Target>`), invokes
     * `allocator(layout)` to obtain the destination `Target` (`Rgba8888Target`,
     * `Rgb888Target`, or `Nv12Target`), reads the transformed pixels into it,
     * and returns the resolved output `Dimensions`.
     *
     * If `request` is invalid for `size()`, returns `INVALID_ARGUMENT` without
     * invoking `allocator`.
     */
    template <typename AllocFn,
              typename StatusOrTarget = std::invoke_result_t<AllocFn, const ReadLayout&>,
              typename Target = typename StatusOrTarget::value_type,
              typename = std::enable_if_t<std::is_same_v<Target, Rgba8888Target> ||
                                          std::is_same_v<Target, Rgb888Target> ||
                                          std::is_same_v<Target, Nv12Target>>>
    absl::StatusOr<Dimensions> ReadInto(const ReadRequest& request, AllocFn&& allocator) const {
        return ReadWithTargetAllocator(request, FormatForTarget<Target>(),
                                       [&](const ReadLayout& layout) -> absl::StatusOr<ReadTarget> {
                                           StatusOrTarget target = allocator(layout);
                                           if (!target.ok()) return target.status();
                                           return ReadTarget{*std::move(target)};
                                       });
    }

    /**
     * Computes the tightly packed destination buffer requirements for `request`
     * and `format` on a frame of native dimensions `frame`.
     *
     * @param frame Native (unrotated) frame dimensions.
     * @param request Desired crop, rotation, and output size.
     * @param format Target pixel format.
     * @return The required plane sizes and offsets, or a zeroed `ReadLayout`
     *         (`dimensions == {0, 0}`, `total_bytes == 0`, every plane empty;
     *         `format` and `plane_count` still describe `format`) if `request`
     *         is invalid for `frame`.
     */
    static ReadLayout LayoutFor(Dimensions frame, const ReadRequest& request, ReadFormat format);

    /**
     * Creates a lightweight, bufferless slot of `size` whose reads fill `dst`
     * with opaque black (`R=0, G=0, B=0, A=255` for RGBA/RGB, or `Y=16,
     * U=128, V=128` for BT.601 limited-range NV12).
     *
     * @param size Native dimensions reported by the blank slot.
     * @return A `Slot` instance that requires no pixel memory allocation.
     */
    static std::unique_ptr<const Slot> Blank(Dimensions size);

  protected:
    /**
     * Backend hook invoked by `ReadInto` after `read` and `dst` have been
     * validated and resolved against `size()`.
     *
     * @param read Validated request with `read.crop` and `read.output`
     *        guaranteed to be populated and in bounds.
     * @param dst Validated destination planes guaranteed to fit `read.output`.
     */
    virtual absl::Status DoReadInto(const ReadRequest& read, const ReadTarget& dst) const = 0;

  private:
    absl::StatusOr<Dimensions> ReadWithTargetAllocator(
            const ReadRequest& request, ReadFormat format,
            absl::FunctionRef<absl::StatusOr<ReadTarget>(const ReadLayout& layout)> allocator)
            const;
};

class SlotPool;

/**
 * Custom `std::unique_ptr` deleter that recycles a `Slot` back to its
 * originating `SlotPool` if the pool is still alive, or deletes the `Slot`
 * directly if the pool has already been replaced or destroyed.
 */
class SlotReturn {
  public:
    SlotReturn() = default;
    explicit SlotReturn(std::weak_ptr<SlotPool> pool) : pool_(std::move(pool)) {}

    void operator()(const Slot* slot) const;

  private:
    std::weak_ptr<SlotPool> pool_;
};

/**
 * Move-only RAII lease granting read-only access to a single captured `Slot`.
 *
 * Architectural Role:
 * `SlotLease` enforces the ownership invariant between producers and consumers:
 * while a `SlotLease` exists, the underlying `Slot` is never written to by the
 * producer. Destroying the `SlotLease` automatically returns the `Slot` to its
 * `SlotPool` via `SlotReturn`, potentially triggering a starvation-recovery
 * capture if the producer was waiting for a free slot.
 *
 * Thread Safety:
 * `dimensions()` may be called on any thread. `ReadInto()` must be called on
 * consumer worker threads (never on the QEMU main loop or display event loop).
 * Destruction is thread-safe on any thread.
 */
class SlotLease final {
  public:
    SlotLease(std::unique_ptr<const Slot, SlotReturn> slot, Dimensions dimensions)
            : slot_(std::move(slot)), dimensions_(dimensions) {}
    /** Wraps a standalone `Slot` with no pool; destroyed when the lease dies. */
    explicit SlotLease(std::unique_ptr<const Slot> slot)
            : slot_(slot.release()), dimensions_(slot_->size()) {}
    /** Creates a bufferless lease of `size` whose reads fill `dst` with black. */
    static SlotLease Blank(Dimensions size) { return SlotLease(Slot::Blank(size)); }

    SlotLease(SlotLease&&) noexcept = default;
    SlotLease& operator=(SlotLease&&) noexcept = default;

    /** @return Native (unrotated) dimensions of the leased frame. */
    Dimensions dimensions() const { return dimensions_; }

    /**
     * Reads and transforms the leased slot's pixels into `dst`.
     *
     * @return `FAILED_PRECONDITION` if called on a moved-from lease, or the
     *         status of `Slot::ReadInto`.
     */
    absl::Status ReadInto(const ReadRequest& request, const ReadTarget& dst) const {
        if (slot_ == nullptr) return absl::FailedPreconditionError("Moved-from SlotLease");
        return slot_->ReadInto(request, dst);
    }

    /**
     * Reads and transforms the leased slot's pixels into a contiguous buffer
     * allocated by `allocator`.
     */
    absl::StatusOr<Dimensions> ReadInto(
            const ReadRequest& request, ReadFormat format,
            absl::FunctionRef<absl::StatusOr<uint8_t*>(const ReadLayout& layout)> allocator) const {
        if (slot_ == nullptr) return absl::FailedPreconditionError("Moved-from SlotLease");
        return slot_->ReadInto(request, format, allocator);
    }

    /**
     * Reads and transforms the leased slot's pixels into a typed `Target`
     * (`Rgba8888Target`, `Rgb888Target`, or `Nv12Target`) returned by
     * `allocator`.
     */
    template <typename AllocFn,
              typename StatusOrTarget = std::invoke_result_t<AllocFn, const ReadLayout&>,
              typename Target = typename StatusOrTarget::value_type,
              typename = std::enable_if_t<std::is_same_v<Target, Rgba8888Target> ||
                                          std::is_same_v<Target, Rgb888Target> ||
                                          std::is_same_v<Target, Nv12Target>>>
    absl::StatusOr<Dimensions> ReadInto(const ReadRequest& request, AllocFn&& allocator) const {
        if (slot_ == nullptr) return absl::FailedPreconditionError("Moved-from SlotLease");
        return slot_->ReadInto(request, std::forward<AllocFn>(allocator));
    }

  private:
    std::unique_ptr<const Slot, SlotReturn> slot_;
    Dimensions dimensions_;
};

/**
 * Consumer-side publication sink implemented by `DisplayFrames` and called by
 * `FrameProducer`.
 *
 * Thread Safety:
 * All methods are O(1), non-blocking, and safe to call from any thread.
 */
class IFrameSink {
  public:
    virtual ~IFrameSink() = default;

    /**
     * Publishes a completed, immutable frame lease and marks the display warm.
     *
     * @param lease Exclusive read-only lease to the captured slot.
     * @param capture_time Timestamp when the capture was initiated.
     */
    virtual void PublishFrame(SlotLease lease, absl::Time capture_time) = 0;

    /**
     * Notifies the sink that the producer was reset (e.g., guest reboot or
     * snapshot load).
     *
     * Every previously published frame (whether backed by a slot or blank) is
     * immediately fenced via the reset epoch so subsequent `ReadInto()` calls
     * on old `FramePtr`s return `UNAVAILABLE`. The sink publishes a single
     * replacement blank frame at the last published dimensions (or the
     * display's configured dimensions if no frame was ever published) and marks
     * the display not warm until the next `PublishFrame` or `PublishNoContent`.
     * Idempotent if no `PublishFrame` occurred since the previous reset.
     */
    virtual void OnProducerReset() = 0;

    /**
     * Notifies the sink that the producer inspected its source and found no
     * guest content to display (e.g., before the guest binds a surface).
     *
     * Unlike `OnProducerReset()`, this does not invalidate earlier frames and
     * marks the display warm. It publishes a blank frame only if the latest
     * frame since the last reset is not already blank.
     */
    virtual void PublishNoContent() = 0;
};

/**
 * Producer control interface implemented by `FrameProducer` and driven by
 * `DisplayFrames`.
 */
class IFrameProducer {
  public:
    virtual ~IFrameProducer() = default;

    /**
     * Updates whether the display has active consumer demand.
     *
     * Must be called on the display event loop. Transitioning to `warm = true`
     * automatically requests an initial frame capture so warming consumers
     * receive current display content even if the guest is idle.
     *
     * @param warm `true` when at least one listener or `WaitForFreshFrame`
     *        caller requires frames (or during the post-demand warm retention
     *        window); `false` after the retention window expires.
     */
    virtual void SetDemand(bool warm) = 0;

    /**
     * Requests a fresh capture of the display's current content, even if the
     * guest has not flushed a new frame.
     *
     * Callable from any thread, non-blocking, and coalesced. If the source has
     * no content, the producer calls `IFrameSink::PublishNoContent()`.
     */
    virtual void RequestFrame() = 0;
};

}  // namespace goldfish::display

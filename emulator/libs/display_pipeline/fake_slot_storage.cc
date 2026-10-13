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
#include "goldfish/display/test/fake_slot_storage.h"

#include <atomic>
#include <cstring>
#include <type_traits>
#include <utility>
#include <variant>

namespace goldfish::display::testing {
namespace {

class FakeSlotBuffer : public Slot {
  public:
    FakeSlotBuffer(Dimensions size, std::shared_ptr<FakeStorageStats> stats)
            : size_(size), stats_(std::move(stats)) {
        absl::MutexLock lock(&stats_->stats_mutex);
        ++stats_->live_buffers;
    }
    ~FakeSlotBuffer() override {
        absl::MutexLock lock(&stats_->stats_mutex);
        --stats_->live_buffers;
    }

    Dimensions size() const override { return size_; }

    uint32_t Tag() const { return tag_.load(std::memory_order_acquire); }
    void SetTag(uint32_t tag) { tag_.store(tag, std::memory_order_release); }

  protected:
    // Writes the slot's content tag (low byte) into every output byte of the
    // first plane.
    absl::Status DoReadInto(const ReadRequest& read, const ReadTarget& dst) const override;

  private:
    const Dimensions size_;
    const std::shared_ptr<FakeStorageStats> stats_;
    std::atomic<uint32_t> tag_{0};
};

absl::Status FakeSlotBuffer::DoReadInto(const ReadRequest& read, const ReadTarget& dst) const {
    {
        absl::MutexLock lock(&stats_->stats_mutex);
        ++stats_->reads;
    }
    const auto byte = static_cast<uint8_t>(Tag());
    const size_t width = read.output.width;
    const auto [plane, row_bytes] = std::visit(
            [&](const auto& target) -> std::pair<const Plane*, size_t> {
                using T = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<T, Nv12Target>) {
                    return {&target.y, width};
                } else if constexpr (std::is_same_v<T, Rgb888Target>) {
                    return {&target.plane, width * 3};
                } else {
                    return {&target.plane, width * 4};
                }
            },
            dst);
    const size_t stride = plane->stride == 0 ? row_bytes : plane->stride;
    for (uint32_t y = 0; y < read.output.height; ++y) {
        std::memset(plane->data.data() + y * stride, byte, row_bytes);
    }
    return absl::OkStatus();
}

}  // namespace

uint32_t ContentTagOf(const SurfaceSource& src) {
    return src.data == nullptr ? 0 : src.data[0];
}

FakeSlotStorage::FakeSlotStorage(std::shared_ptr<FakeStorageStats> stats)
        : stats_(std::move(stats)) {}

void FakeSlotStorage::set_complete_inline(bool complete_inline) {
    absl::MutexLock lock(&storage_mutex_);
    complete_inline_ = complete_inline;
}

void FakeSlotStorage::set_drop_completions(bool drop) {
    absl::MutexLock lock(&storage_mutex_);
    drop_completions_ = drop;
}

void FakeSlotStorage::FailNextAllocation(absl::Status status) {
    absl::MutexLock lock(&storage_mutex_);
    next_allocation_error_ = std::move(status);
}

int FakeSlotStorage::PendingCaptures() {
    absl::MutexLock lock(&storage_mutex_);
    return static_cast<int>(pending_.size());
}

std::optional<FakeSlotStorage::PendingCapture> FakeSlotStorage::PopPending() {
    absl::MutexLock lock(&storage_mutex_);
    if (pending_.empty()) return std::nullopt;
    PendingCapture capture = std::move(pending_.front());
    pending_.pop_front();
    return capture;
}

void FakeSlotStorage::Complete(PendingCapture capture, absl::Status status) {
    if (status.ok()) static_cast<FakeSlotBuffer&>(*capture.dst).SetTag(capture.tag);
    const Dimensions size = capture.dst->size();
    std::move(capture.done)(CaptureResult{.status = std::move(status), .dimensions = size});
}

bool FakeSlotStorage::CompleteNext(absl::Status status) {
    std::optional<PendingCapture> capture = PopPending();
    if (!capture.has_value()) return false;
    Complete(*std::move(capture), std::move(status));
    return true;
}

bool FakeSlotStorage::DropNext() {
    std::optional<PendingCapture> capture = PopPending();
    return capture.has_value();  // Destroys `done` uncalled, outside the lock.
}

absl::StatusOr<std::unique_ptr<Slot>> FakeSlotStorage::Allocate(Dimensions size) {
    {
        absl::MutexLock lock(&storage_mutex_);
        if (next_allocation_error_.has_value()) {
            absl::Status error = *std::move(next_allocation_error_);
            next_allocation_error_.reset();
            return error;
        }
    }
    {
        absl::MutexLock lock(&stats_->stats_mutex);
        ++stats_->allocations;
    }
    return std::make_unique<FakeSlotBuffer>(size, stats_);
}

void FakeSlotStorage::Capture(Slot& dst, const SurfaceSource& src,
                              absl::AnyInvocable<void(CaptureResult) &&> done) {
    {
        absl::MutexLock lock(&stats_->stats_mutex);
        ++stats_->captures;
    }
    // Declared before the lock so a dropped `done` is destroyed after unlocking.
    PendingCapture capture{&dst, ContentTagOf(src), std::move(done)};
    {
        absl::MutexLock lock(&storage_mutex_);
        if (drop_completions_) return;
        if (!complete_inline_) {
            pending_.push_back(std::move(capture));
            return;
        }
    }
    Complete(std::move(capture), absl::OkStatus());
}

}  // namespace goldfish::display::testing

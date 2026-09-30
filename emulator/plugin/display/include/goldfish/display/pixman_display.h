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

#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/display.h"
#include "goldfish/display/frame_producer.h"
#include "goldfish/display/frame_source.h"
#include "goldfish/display/legacy_surface_source.h"
#include "goldfish/display/pixman_image_ptr.h"
#include "goldfish/display/slot_storage.h"
#include "goldfish/fps_calculator.h"

namespace goldfish::display {

class PixmanDisplay : public IDisplay {
  public:
    PixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image, uint32_t dpi = 0,
                  uint32_t flags = 0);
    PixmanDisplay(EventLoop* loop, int id, const PixmanImagePtr& image, uint32_t dpi = 0,
                  uint32_t flags = 0);
    ~PixmanDisplay() override;

    virtual void UpdateSourceImage(::pixman_image_t* image) ABSL_LOCKS_EXCLUDED(pixman_mutex_);
    void UpdateSurface(int x, int y, int width, int height) ABSL_LOCKS_EXCLUDED(pixman_mutex_);

    std::pair<int, int> ResizeKeepAspectRatio(int desired_width, int desired_height) override;

  protected:
    // Forward to the frame producer.
    void OnDemand(bool warm) override;
    void OnFrameRequested() override;

    // @param executor Runs the frame producer's posted pumps.
    PixmanDisplay(EventLoop* loop, EventLoop* executor, int id, ::pixman_image_t* image,
                  uint32_t dpi = 0, uint32_t flags = 0);

    // The LegacySurfaceSource hooks, called by the frame producer's pump.
    // The default captures source_image_.
    virtual void RefreshSource() {}
    virtual void OnSourceDemandChanged(bool /*warm*/) {}
    virtual IFrameSource::Start CaptureSource(LegacySurfaceSource& source)
            ABSL_LOCKS_EXCLUDED(pixman_mutex_);

    // Stops every hook call (IDisplay's and the frame producer's) and waits
    // for one in progress. Idempotent. The hooks are virtual, so the
    // most-derived destructor must call it first, before destruction changes
    // the vtable or tears down state the hooks use.
    void ShutdownProducer();

    static SurfaceSource SurfaceSourceFromPixman(::pixman_image_t* image);

    PixmanImagePtr SourceImage() const ABSL_LOCKS_EXCLUDED(pixman_mutex_) {
        const absl::MutexLock lock(&pixman_mutex_);
        return source_image_;
    }

    template <typename Sink>
    friend void AbslStringify(Sink&, const PixmanDisplay&);

    // Declared before legacy_, which sends it re-capture requests.
    const std::shared_ptr<FrameProducer> frame_producer_;
    const std::shared_ptr<LegacySurfaceSource> legacy_;
    PixmanImagePtr source_image_ ABSL_GUARDED_BY(pixman_mutex_);
    ::goldfish::FpsCalculator fps_calculator_{30};

    // Acquired before frame_producer_'s publish_mutex_ when an inline surface
    // capture publishes or when a subclass resets under this lock.
    mutable absl::Mutex pixman_mutex_;
};

template <typename Sink>
void AbslStringify(Sink& sink, const PixmanDisplay& display) {
    absl::Format(&sink, "%s", display.String());
}

}  // namespace goldfish::display

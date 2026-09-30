// Copyright 2026 The Android Open Source Project
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

#include "goldfish/display/display.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"

namespace goldfish::display {

// Test access to what IDisplay keeps from consumers: its sink and its producer.
class DisplayTestPeer {
  public:
    // Null for a display without a loop.
    static const std::shared_ptr<DisplayFrames>& frames(const IDisplay& display) {
        return display.frames_;
    }
    // The producer that DisplayFrames drives, as if DisplayFrames called it.
    static std::shared_ptr<IFrameProducer> producer(const IDisplay& display) {
        return display.producer_;
    }
};

}  // namespace goldfish::display

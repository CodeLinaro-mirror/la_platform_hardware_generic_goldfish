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

#include "emulator/libs/display/include/goldfish/display/test/fake_multi_display.h"

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "emulator/libs/display/include/goldfish/display/test/fake_pixman_display.h"

namespace goldfish::display::test {

FakeMultiDisplay::FakeMultiDisplay(EventLoop* loop) : IMultiDisplay(loop), mEnabled(true) {
    // Create the default display (display_id 0)
    mDisplays[0] = ActiveFakePixmanDisplay::createShared(loop_, 0, 30, 640, 480);
}

absl::StatusOr<DisplayPtr> FakeMultiDisplay::CreateDisplay(DisplayId display_id, uint32_t width,
                                                           uint32_t height, uint32_t dpi,
                                                           uint32_t flags) {
    if (mDisplays.count(display_id)) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Display with id %d already exists", display_id));
    }
    auto sharedDisplay =
            ActiveFakePixmanDisplay::createShared(loop_, display_id, 30, width, height);
    mDisplays[display_id] = sharedDisplay;
    FireEvent(DisplayEvent{DisplayEvent::AddedEvent{sharedDisplay}});
    return sharedDisplay;
}

bool FakeMultiDisplay::IsEnabled() const {
    return mEnabled;
}

absl::StatusOr<DisplayPtr> FakeMultiDisplay::GetDisplay(DisplayId display_id) const {
    if (!mDisplays.count(display_id)) {
        return absl::NotFoundError(absl::StrFormat("Display with id %d not found", display_id));
    }
    return mDisplays.at(display_id);
}

absl::Status FakeMultiDisplay::EraseDisplay(DisplayId display_id) {
    if (display_id == 0) {
        return absl::InvalidArgumentError("Cannot delete default display");
    }
    if (mDisplays.erase(display_id) == 0) {
        return absl::NotFoundError(absl::StrFormat("Display with id %d not found", display_id));
    }
    FireEvent({DisplayEvent{DisplayEvent::DeletedEvent{display_id}}});
    return absl::OkStatus();
}

std::vector<DisplayPtr> FakeMultiDisplay::Displays() const {
    std::vector<DisplayPtr> result;
    for (const auto& [id, display] : mDisplays) {
        result.push_back(display);
    }
    return result;
}

void FakeMultiDisplay::clear() {
    // Iterate through the map and erase all elements except the default display (ID 0)
    for (auto it = mDisplays.begin(); it != mDisplays.end();) {
        if (it->first != 0) {
            it = mDisplays.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace goldfish::display::test

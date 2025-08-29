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
#include "android/goldfish/display/Display.h"

namespace android::goldfish {
class NullDisplay : public IDisplay {
  public:
    NullDisplay() : IDisplay(nullptr, -1, -1, -1) { mActive = false; }
    ~NullDisplay() = default;

    absl::StatusOr<FrameInfo> getPixels(PixelFormat fmt, int width, int height, int rotationDeg,
                                        uint8_t* pixel, size_t* cPixels) const override {
        return absl::InvalidArgumentError("This display does not exist.");
    }

    void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override {}

    void sendMouseEvent(int x, int y, int button_mask) override {};

    void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override {};

    std::string string() const override { return "NullDisplay"; }
};
}  // namespace android::goldfish
// Copyright (C) 2024 The Android Open Source Project
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
#include <pixman.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "absl/log/log.h"
#include "google/protobuf/util/message_differencer.h"

#include "android/emulation/control/display/DisplayChangeListener.h"
#include "android/goldfish/display/SharedMemoryLibrary.h"

namespace android {
namespace goldfish {

using Image = android::emulation::control::Image;
using ImageFormat = android::emulation::control::ImageFormat;
using ImageEventSupport =
    android::emulation::control::EventChangeSupport<Image>;

struct ImageFormatHash {
  size_t operator()(const ImageFormat &f) const {
    // Combine hashes of individual members
    size_t h1 = std::hash<int>{}(f.width());
    size_t h2 = std::hash<int>{}(f.height());
    size_t h3 = std::hash<int>()(f.display());
    size_t h4 = std::hash<int>()((int)f.format());
    auto t = f.transport();

    size_t h5 = std::hash<int>{}((int)t.channel());
    size_t h6 = std::hash<std::string>{}(t.handle());
    return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
  }
};

struct ProtobufMessageEqual {
  bool operator()(const ImageFormat &lhs, const ImageFormat &rhs) const {
    return google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
  }
};

class QemuDisplayTransformer
    : public android::emulation::control::DisplayChangeListener {

public:
  QemuDisplayTransformer() = default;
  ~QemuDisplayTransformer() override = default;

  ImageEventSupport *addListener(ImageFormat fmt) override;
  Image getScreenshot(ImageFormat fmt) override;

  void fireEvent(pixman_image_t *src_img);

private:
  SharedMemoryLibrary mSharedMemoryLibrary;
  std::mutex mListenerLock;
  std::unordered_map<ImageFormat,
                     std::tuple<std::unique_ptr<ImageEventSupport>, Image>,
                     ImageFormatHash, ProtobufMessageEqual>
      mListenerMap;
};

} // namespace goldfish
} // namespace android
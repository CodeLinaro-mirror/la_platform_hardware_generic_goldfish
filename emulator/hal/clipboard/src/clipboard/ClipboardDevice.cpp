// Copyright 2024 The Android Open Source Project
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
#include "android/clipboard/ClipboardDevice.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/synchronization/mutex.h"

#include "android/goldfish/config/avd.h"
#include "goldfish/devices/qemud.h"

using android::goldfish::Avd;

namespace goldfish::devices::clipboard {

class ClipboardDevice : public IClipboardDevice {
  public:
    ClipboardDevice() { VLOG(1) << "Clipboard device has been created"; }

    void onConnect() override { VLOG(1) << "Clipboard device has been connected"; }
    void onClose() override { VLOG(1) << "Clipboard device has been disconnected"; }

    void onReceive(const std::string_view data) override {
        mReceiveData.insert(mReceiveData.end(), data.begin(), data.end());

        while (true) {
            if (mReceiveData.size() < sizeof(uint32_t)) {
                return;
            }

            const uint32_t dataSize = absl::little_endian::Load32(mReceiveData.data());
            if (mReceiveData.size() < (sizeof(uint32_t) + dataSize)) {
                return;
            }

            std::string clipboardData(&mReceiveData[sizeof(uint32_t)], dataSize);
            mReceiveData.erase(mReceiveData.begin(),
                            mReceiveData.begin() + sizeof(uint32_t) + dataSize);

            VLOG(1) << "Clipboard update from guest to (" << dataSize << "):" << clipboardData;

            {
                absl::MutexLock lock(&mClipboardDataLock);
                mClipboardData = clipboardData;
            }

            fireEvent(clipboardData);
        }
    }

    bool isEnabled() const override { return mEnabled; }

    void enable(bool enable) override { mEnabled = enable; }

    void setContents(ClipboardData contents) override {
        if (!mEnabled) {
            return;
        }
        VLOG(1) << "Clipboard update from host to (" << contents.size() << "):" << contents;
        uint32_t size = contents.size();

        // Ensure little-endian representation
        char size_buf[sizeof(uint32_t)];
        absl::little_endian::Store32(size_buf, size);

        socket()->send(std::string(size_buf, sizeof(size_buf)));
        socket()->send(std::string(contents));
    }

    ClipboardData getContents() const override {
        absl::MutexLock lock(&mClipboardDataLock);
        return mClipboardData;
    };

  private:
    std::vector<char> mReceiveData;
    std::string mClipboardData ABSL_GUARDED_BY(mClipboardDataLock);
    bool mEnabled{true};
    mutable absl::Mutex mClipboardDataLock;  // protects mClipboardData
};

void IClipboardDevice::registerDevice(IConnectorRegistry* registry, EventLoop* clientLoop,
                                      EventLoop* qemuLoop) {
    registry->registerHalDevice(std::string(IClipboardDevice::serviceName), clientLoop, qemuLoop,
                                [] { return std::make_shared<ClipboardDevice>(); });
}

}  // namespace goldfish::devices::clipboard
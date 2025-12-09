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
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"

namespace goldfish::devices::clipboard {

using ::goldfish::avd_universe::clipboard::ClipboardChannel;
using ::goldfish::avd_universe::clipboard::ClipboardData;
using ::goldfish::avd_universe::clipboard::ObservableClipboardData;

using ClipboardDataUpdateSubscription = std::unique_ptr<
        android::base::eventing::ScopedEventCallback<ObservableClipboardData, ClipboardData>>;

class ClipboardDevice : public IClipboardDevice {
  public:
    ClipboardDevice(ClipboardChannel* clipboardChannel) : mClipboardChannel(*clipboardChannel) {
        VLOG(1) << "Clipboard device has been created";
    }

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

            ClipboardData clipboardData;
            clipboardData.contents = std::string(&mReceiveData[sizeof(uint32_t)], dataSize);
            mReceiveData.erase(mReceiveData.begin(),
                               mReceiveData.begin() + sizeof(uint32_t) + dataSize);

            VLOG(1) << "Clipboard update from guest to (" << dataSize << "):" << clipboardData;
            mClipboardChannel.guestToHost.setValue(std::move(clipboardData));
        }
    }

    void setContents(const ClipboardData& clip) {
        const uint32_t size = clip.contents.size();
        VLOG(1) << "Clipboard update from host to (" << size << "):" << clip.contents;

        // Ensure little-endian representation
        char size_buf[sizeof(uint32_t)];
        absl::little_endian::Store32(size_buf, size);
        socket()->send(std::string(size_buf, sizeof(size_buf)));
        socket()->send(clip.contents);
    }

    void setClipboardChangeSubscription(ClipboardDataUpdateSubscription subscription) {
        mClipboardDataUpdateSubscription = std::move(subscription);
    }

  private:
    std::vector<char> mReceiveData;
    ClipboardChannel& mClipboardChannel;
    ClipboardDataUpdateSubscription mClipboardDataUpdateSubscription;
};

void IClipboardDevice::registerDevice(avd_universe::clipboard::ClipboardChannel* channel,
                                      IConnectorRegistry* registry, EventLoop* clientLoop,
                                      EventLoop* qemuLoop) {
    registry->registerHalDevice(
            std::string(ClipboardDevice::serviceName), clientLoop, qemuLoop, [channel] {
                auto dev = std::make_shared<ClipboardDevice>(channel);
                std::weak_ptr<ClipboardDevice> weakDev = dev;

                auto clipboardChangeSubscription = makeScopedCallback(
                        channel->hostToGuest,
                        [weakDev = std::move(weakDev)](const ClipboardData& clip) {
                            if (const auto dev = weakDev.lock()) {
                                dev->setContents(clip);
                            }
                        });

                dev->setClipboardChangeSubscription(std::move(clipboardChangeSubscription));

                return dev;
            });
}

}  // namespace goldfish::devices::clipboard

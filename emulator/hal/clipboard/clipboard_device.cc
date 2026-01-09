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

#include "goldfish/devices/clipboard/clipboard_device.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"

namespace goldfish::devices::clipboard {

using ::goldfish::avd_universe::clipboard::ClipboardChannel;
using ::goldfish::avd_universe::clipboard::ClipboardData;
using ::goldfish::avd_universe::clipboard::ObservableClipboardData;

using ClipboardDataUpdateSubscription = std::unique_ptr<
        android::base::eventing::ScopedEventCallback<ObservableClipboardData, ClipboardData>>;

class ClipboardDevice : public IClipboardDevice {
  public:
    explicit ClipboardDevice(ClipboardChannel* clipboard_channel)
            : clipboard_channel_(*clipboard_channel) {
        VLOG(1) << "Clipboard device has been created";
    }

    void OnConnect() override { VLOG(1) << "Clipboard device has been connected"; }
    void OnClose() override { VLOG(1) << "Clipboard device has been disconnected"; }

    void OnReceive(const std::string_view data) override {
        receive_data_.insert(receive_data_.end(), data.begin(), data.end());

        while (true) {
            if (receive_data_.size() < sizeof(uint32_t)) {
                return;
            }

            const uint32_t data_size = absl::little_endian::Load32(receive_data_.data());
            if (receive_data_.size() < (sizeof(uint32_t) + data_size)) {
                return;
            }

            ClipboardData clipboard_data;
            clipboard_data.contents = std::string(&receive_data_[sizeof(uint32_t)], data_size);
            receive_data_.erase(receive_data_.begin(),
                                receive_data_.begin() + sizeof(uint32_t) + data_size);

            VLOG(1) << "Clipboard update from guest to (" << data_size << "):" << clipboard_data;
            clipboard_channel_.guest_to_host.SetValue(std::move(clipboard_data));
        }
    }

    void SetContents(const ClipboardData& clip) {
        const uint32_t size = clip.contents.size();
        VLOG(1) << "Clipboard update from host to (" << size << "):" << clip.contents;

        // Ensure little-endian representation
        char size_buf[sizeof(uint32_t)];
        absl::little_endian::Store32(size_buf, size);
        Socket()->Send(std::string(size_buf, sizeof(size_buf)));
        Socket()->Send(clip.contents);
    }

    void SetClipboardChangeSubscription(ClipboardDataUpdateSubscription subscription) {
        clipboard_data_update_subscription_ = std::move(subscription);
    }

  private:
    std::vector<char> receive_data_;
    ClipboardChannel& clipboard_channel_;
    ClipboardDataUpdateSubscription clipboard_data_update_subscription_;
};

void IClipboardDevice::RegisterDevice(avd_universe::clipboard::ClipboardChannel* channel,
                                      IConnectorRegistry* registry, EventLoop* client_loop,
                                      EventLoop* qemu_loop) {
    registry->RegisterHalDevice(
            std::string(ClipboardDevice::kServiceName), client_loop, qemu_loop,
            [channel](std::string_view /*args*/) {
                auto dev = std::make_shared<ClipboardDevice>(channel);
                std::weak_ptr<ClipboardDevice> weak_dev = dev;

                auto change_subscription = makeScopedCallback(
                        channel->host_to_guest,
                        [weak_dev = std::move(weak_dev)](const ClipboardData& clip) {
                            if (const auto dev = weak_dev.lock()) {
                                dev->SetContents(clip);
                            }
                        });

                dev->SetClipboardChangeSubscription(std::move(change_subscription));

                return dev;
            });
}

}  // namespace goldfish::devices::clipboard

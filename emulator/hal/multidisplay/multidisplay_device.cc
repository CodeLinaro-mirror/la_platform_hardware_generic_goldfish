// Copyright (C) 2026 The Android Open Source Project
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
#include "goldfish/devices/multidisplay/multidisplay_device.h"

#include <cstring>
#include <vector>

#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/avd_info/avd_info.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
extern "C" {
#include "goldfish/avd/rutabaga_glue.h"
}  // extern "C"


namespace goldfish::devices::multidisplay {

// --- External API Implementation ---

void ReadDisplayColorBuffer(uint32_t colorbuffer, uint8_t* outptr) {
    auto device = avd_info::getAvd().getActiveMultiDisplayDevice();

    if (device) {
        device->ReadDisplayColorBufferInternal(colorbuffer, outptr);
    }

}

uint32_t GetDisplayColorBuffer(uint32_t displayId) {
    auto device = avd_info::getAvd().getActiveMultiDisplayDevice();

    if (device) {
        return device->GetColorBufferInternal(displayId);
    }

    // Device not connected or display not found
    return 0;
}

void SendToGuest(std::string message) {
    auto device = avd_info::getAvd().getActiveMultiDisplayDevice();

    if (device) {
        device->SendInternal(std::move(message));
    } else {
        LOG(WARNING) << "Dropped multidisplay message: Guest not connected.";
    }
}

// Helpers for specific commands
void SendAddDisplay(uint32_t display_id, uint32_t width, uint32_t height, uint32_t dpi,
                    uint32_t flag) {
    auto device = avd_info::getAvd().getActiveMultiDisplayDevice();

    if (device) {
        device->AddDisplayInternal(display_id, width, height, dpi, flag);
        device->SendAddDisplayPacket(display_id, width, height, dpi, flag);
    } else {
        LOG(WARNING) << "Dropped multidisplay message: Guest not connected.";
    }
}

void SendDelDisplay(uint32_t display_id) {
    // 1. Clean up local state first
    auto device = avd_info::getAvd().getActiveMultiDisplayDevice();

    if (device) {
        device->RemoveColorBufferInternal(display_id);
    }
    std::string msg(1 + sizeof(uint32_t), '\0');
    msg[0] = MultiDisplayDevice::kCmdDel;
    uint32_t* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = display_id;
    SendToGuest(std::move(msg));
}

void SendSetDisplay(uint32_t mode_id, uint32_t width, uint32_t height, uint32_t dpi,
                    uint32_t flag) {
    std::string msg(1 + 5 * sizeof(uint32_t), '\0');
    msg[0] = MultiDisplayDevice::kCmdSetDisplay;
    uint32_t* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = mode_id;
    data[1] = width;
    data[2] = height;
    data[3] = dpi;
    data[4] = flag;
    SendToGuest(std::move(msg));
}

// --- Device Implementation ---

MultiDisplayDevice::MultiDisplayDevice(async::EventLoop* clientLoop) : client_loop_(clientLoop) {
    VLOG(1) << "MultiDisplayDevice created";
}

MultiDisplayDevice::~MultiDisplayDevice() {
    VLOG(1) << "MultiDisplayDevice destroyed";
}

void MultiDisplayDevice::ReadDisplayColorBufferInternal(uint32_t colorbuffer, uint8_t* outptr) {
    absl::MutexLock lock(&color_buffer_mutex_);
    auto it = frame_buffers_.find(colorbuffer);
    if (it != frame_buffers_.end()) {
        memcpy(outptr, it->second.data(), it->second.size());
    } else {
        LOG(WARNING) << "ReadDisplayColorBufferInternal: No framebuffer found for handle "
                     << colorbuffer;
    }
}

uint32_t MultiDisplayDevice::GetColorBufferInternal(uint32_t displayId) {
    absl::MutexLock lock(&color_buffer_mutex_);
    auto it = display_buffers_.find(displayId);
    if (it != display_buffers_.end()) {
        return it->second.cb_handle;
    }
    return 0;  // Return 0 if not found
}

void MultiDisplayDevice::RemoveColorBufferInternal(uint32_t displayId) {
    absl::MutexLock lock(&color_buffer_mutex_);
    auto it = display_buffers_.find(displayId);
    if (it != display_buffers_.end()) {
        if (it->second.cb_handle != 0) {
            frame_buffers_.erase(it->second.cb_handle);
        }
        display_buffers_.erase(it);
    }
}

void MultiDisplayDevice::AddDisplayInternal(uint32_t displayId, uint32_t width, uint32_t height,
                                            uint32_t dpi, uint32_t flag) {
    absl::MutexLock lock(&color_buffer_mutex_);
    display_buffers_[displayId] = {width, height, dpi, flag};
}

void MultiDisplayDevice::OnConnect() {
    VLOG(1) << "MultiDisplay guest connected";
}

void MultiDisplayDevice::OnClose() {
    VLOG(1) << "MultiDisplay guest disconnected";
    receive_buffer_.clear();
}

void MultiDisplayDevice::OnReceive(std::string_view data) {
    // Append the incoming string_view data to the byte vector
    const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(data.data());
    receive_buffer_.insert(receive_buffer_.end(), raw_data, raw_data + data.size());

    while (receive_buffer_.size() >= sizeof(uint32_t)) {
        uint32_t payloadSize;
        std::memcpy(&payloadSize, receive_buffer_.data(), sizeof(uint32_t));

        if (receive_buffer_.size() < sizeof(uint32_t) + payloadSize) {
            // Not enough data for the full payload yet, wait for more
            break;
        }

        // Extract the payload (starting after the 4-byte size header)
        const uint8_t* payloadData = receive_buffer_.data() + sizeof(uint32_t);

        if (payloadSize > 0) {
            uint8_t cmd = payloadData[0];
            switch (cmd) {
            case kCmdQuery: {
                VLOG(1) << "Guest queried for displays.";
                std::vector<std::pair<uint32_t, DisplayInfo>> displays;
                {
                    absl::MutexLock lock(&color_buffer_mutex_);
                    for (const auto& it : display_buffers_) {
                        displays.push_back({it.first, it.second});
                    }
                }
                VLOG(1) << "we have "<<displays.size() << " displays";
                for (const auto& [displayId, info] : displays) {
                    SendAddDisplayPacket(displayId, info.width, info.height, info.dpi, info.flag);
                    VLOG(1) << "Send display " << displayId << " w " << info.width << " h "
                            << info.height;
                }
                break;
            }
            case kCmdBind: {
                if (payloadSize >= 1 + 2 * sizeof(uint32_t)) {
                    // Cast the data directly from the byte array
                    const uint32_t* bindData = reinterpret_cast<const uint32_t*>(payloadData + 1);
                    uint32_t displayId = bindData[0];
                    uint32_t cbHandle = bindData[1];
                    VLOG(1) << "Guest bound display " << displayId << " to handle " << cbHandle;
                    // --- STORE IN MAP ---
                    absl::MutexLock lock(&color_buffer_mutex_);
                    {
                        uint32_t width = 1080;
                        uint32_t height = 1920;
                        auto it = display_buffers_.find(displayId);
                        if (it != display_buffers_.end()) {
                            width = it->second.width;
                            height = it->second.height;
                            it->second.cb_handle = cbHandle;
                            auto& fb = frame_buffers_[cbHandle];
                            fb.resize(width * height * 4);
                            struct rutabaga* vr = rutabagaGetInstance();
                            const int stride = width * 4;
                            rutabagaImageRead(vr, cbHandle, width, height, stride, (void*)fb.data(),
                                              fb.size());
                        } else {
                            LOG(WARNING) << "BIND received for unknown display " << displayId
                                         << ", ignored";
                        }
                    }
                } else {
                    LOG(ERROR) << "Malformed BIND command received.";
                }
                break;
            }
            default:
                LOG(WARNING) << "Unknown command from guest: " << static_cast<int>(cmd);
                break;
            }
        }

        // Remove the processed message from the front of the vector
        receive_buffer_.erase(receive_buffer_.begin(),
                              receive_buffer_.begin() + sizeof(uint32_t) + payloadSize);
    }
}

void MultiDisplayDevice::SendAddDisplayPacket(uint32_t displayId, uint32_t width, uint32_t height,
                                              uint32_t dpi, uint32_t flag) {
    std::string msg(1 + 5 * sizeof(uint32_t), '\0');
    msg[0] = kCmdAdd;
    uint32_t* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = displayId;
    data[1] = width;
    data[2] = height;
    data[3] = dpi;
    data[4] = flag;
    SendInternal(std::move(msg));
}

void MultiDisplayDevice::SendInternal(std::string message) {
    // Ensure we run on the correct thread (Client Loop) before touching the socket
    if (!client_loop_->IsOnLoopThread()) {
        client_loop_
                ->Post([self = shared_from_this(), msg = std::move(message)]() mutable {
                    self->SendFramed(std::move(msg));
                })
                .IgnoreError();
        return;
    }

    SendFramed(std::move(message));
}

void MultiDisplayDevice::SendFramed(std::string msg) {
    // Protocol: 4 bytes Little Endian Length + Payload
    char sizeBuf[sizeof(uint32_t)];
    // Assuming absl::little_endian
    absl::little_endian::Store32(sizeBuf, msg.size());

    Socket()->Send(std::string(sizeBuf, sizeof(sizeBuf)));
    Socket()->Send(std::move(msg));
}

// --- Registration ---

void MultiDisplayDevice::RegisterDevice(std::shared_ptr<MultiDisplayDevice> device,
                                        IConnectorRegistry* registry, async::EventLoop* client_loop,
                                        async::EventLoop* qemu_loop) {
    registry->RegisterHalDevice("multidisplay", client_loop, qemu_loop,
                                [device](std::string_view /*args*/) { return device; });
}

}  // namespace goldfish::devices::multidisplay

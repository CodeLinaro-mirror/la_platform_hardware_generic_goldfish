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

extern "C" {
#include "goldfish/devices/virtio_gpu_gfxstream/virtio_gpu_glue.h"
}  // extern "C"

namespace goldfish::devices::multidisplay {

// --- Device Implementation ---

MultiDisplayDevice::MultiDisplayDevice(async::EventLoop* client_loop, async::EventLoop* qemu_loop)
        : client_loop_(client_loop), qemu_loop_(qemu_loop) {
    VLOG(1) << "MultiDisplayDevice created";
}

MultiDisplayDevice::~MultiDisplayDevice() {
    VLOG(1) << "MultiDisplayDevice destroyed";
}

void MultiDisplayDevice::ReadDisplayColorBuffer(uint32_t colorbuffer, uint8_t* outptr) {
    const absl::MutexLock lock(&color_buffer_mutex_);
    auto it = frame_buffers_.find(colorbuffer);
    if (it != frame_buffers_.end()) {
        memcpy(outptr, it->second.data(), it->second.size());
    } else {
        LOG(WARNING) << "ReadDisplayColorBuffer: No framebuffer found for handle " << colorbuffer;
    }
}

uint32_t MultiDisplayDevice::GetDisplayColorBuffer(uint32_t display_id) {
    const absl::MutexLock lock(&color_buffer_mutex_);
    auto it = display_buffers_.find(display_id);
    if (it != display_buffers_.end()) {
        return it->second.cb_handle;
    }
    return 0;  // Return 0 if not found
}

void MultiDisplayDevice::SendAddDisplay(uint32_t display_id, uint32_t width, uint32_t height,
                                        uint32_t dpi, uint32_t flag) {
    AddDisplayInternal(display_id, width, height, dpi, flag);
    SendAddDisplayPacket(display_id, width, height, dpi, flag);
}

void MultiDisplayDevice::SendDelDisplay(uint32_t display_id) {
    RemoveColorBufferInternal(display_id);
    std::string msg(1 + sizeof(uint32_t), '\0');
    msg[0] = kCmdDel;
    auto* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = display_id;
    SendToGuest(std::move(msg));
}

void MultiDisplayDevice::SendSetDisplay(uint32_t mode_id, uint32_t width, uint32_t height,
                                        uint32_t dpi, uint32_t flag) {
    std::string msg(1 + (5 * sizeof(uint32_t)), '\0');
    msg[0] = kCmdSetDisplay;
    auto* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = mode_id;
    data[1] = width;
    data[2] = height;
    data[3] = dpi;
    data[4] = flag;
    SendToGuest(std::move(msg));
}

void MultiDisplayDevice::RemoveColorBufferInternal(uint32_t display_id) {
    const absl::MutexLock lock(&color_buffer_mutex_);
    auto it = display_buffers_.find(display_id);
    if (it != display_buffers_.end()) {
        if (it->second.cb_handle != 0) {
            frame_buffers_.erase(it->second.cb_handle);
        }
        display_buffers_.erase(it);
    }
}

void MultiDisplayDevice::AddDisplayInternal(uint32_t display_id, uint32_t width, uint32_t height,
                                            uint32_t dpi, uint32_t flag) {
    const absl::MutexLock lock(&color_buffer_mutex_);
    display_buffers_[display_id] = {.width = width, .height = height, .dpi = dpi, .flag = flag};
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
    const auto* raw_data = reinterpret_cast<const uint8_t*>(data.data());
    receive_buffer_.insert(receive_buffer_.end(), raw_data, raw_data + data.size());

    while (receive_buffer_.size() >= sizeof(uint32_t)) {
        uint32_t payload_size;
        std::memcpy(&payload_size, receive_buffer_.data(), sizeof(uint32_t));

        if (receive_buffer_.size() < sizeof(uint32_t) + payload_size) {
            // Not enough data for the full payload yet, wait for more
            break;
        }

        // Extract the payload (starting after the 4-byte size header)
        const uint8_t* payload_data = receive_buffer_.data() + sizeof(uint32_t);

        if (payload_size > 0) {
            const uint8_t cmd = payload_data[0];
            switch (cmd) {
            case kCmdQuery: {
                VLOG(1) << "Guest queried for displays.";
                std::vector<std::pair<uint32_t, DisplayInfo>> displays;
                {
                    const absl::MutexLock lock(&color_buffer_mutex_);
                    for (const auto& it : display_buffers_) {
                        displays.emplace_back(it.first, it.second);
                    }
                }
                VLOG(1) << "we have " << displays.size() << " displays";
                for (const auto& [display_id, info] : displays) {
                    SendAddDisplayPacket(display_id, info.width, info.height, info.dpi, info.flag);
                    VLOG(1) << "Send display " << display_id << " w " << info.width << " h "
                            << info.height;
                }
                break;
            }
            case kCmdBind: {
                if (payload_size >= 1 + 2 * sizeof(uint32_t)) {
                    uint32_t display_id = 0;
                    uint32_t cb_handle = 0;
                    std::memcpy(&display_id, payload_data + 1, sizeof(uint32_t));
                    std::memcpy(&cb_handle, payload_data + 1 + sizeof(uint32_t), sizeof(uint32_t));
                    VLOG(1) << "Guest bound display " << display_id << " to handle " << cb_handle;

                    uint32_t width = 0;
                    uint32_t height = 0;
                    bool should_readback = false;
                    {
                        const absl::MutexLock lock(&color_buffer_mutex_);
                        auto it = display_buffers_.find(display_id);
                        if (it != display_buffers_.end()) {
                            const uint32_t prev_cb = it->second.cb_handle;
                            if (prev_cb != 0 && prev_cb != cb_handle) {
                                bool still_referenced = false;
                                for (const auto& [other_id, other_info] : display_buffers_) {
                                    if (other_id != display_id && other_info.cb_handle == prev_cb) {
                                        still_referenced = true;
                                        break;
                                    }
                                }
                                if (!still_referenced) {
                                    frame_buffers_.erase(prev_cb);
                                }
                            }
                            width = it->second.width;
                            height = it->second.height;
                            it->second.cb_handle = cb_handle;
                            if (cb_handle != 0) {
                                auto& fb = frame_buffers_[cb_handle];
                                fb.resize(static_cast<size_t>(width) * height * 4);
                                should_readback = (width > 0 && height > 0);
                            }
                        } else {
                            LOG(WARNING) << "BIND received for unknown display " << display_id
                                         << ", ignored";
                        }
                    }

                    // virtioGpuGlueGetInstance() / virtioGpuGlueImageRead() touch QOM
                    // and the virtio-gpu renderer resource table, which require the
                    // QEMU main thread (BQL). Dispatch onto qemu_loop_ without
                    // blocking client_loop_ on bql_lock() during shutdown.
                    if (should_readback) {
                        auto perform_readback = [weak_self = weak_from_this(), display_id,
                                                 cb_handle, width, height]() {
                            auto self = weak_self.lock();
                            if (!self) {
                                return;
                            }
                            const size_t fb_bytes = static_cast<size_t>(width) * height * 4;
                            std::vector<uint8_t> pixels(fb_bytes);
                            struct VirtioGpuGlue* gpu = virtioGpuGlueGetInstance();
                            if (!gpu) {
                                return;
                            }
                            const uint32_t stride = width * 4;
                            if (virtioGpuGlueImageRead(gpu, cb_handle, width, height, stride,
                                                       pixels.data(),
                                                       static_cast<uint32_t>(pixels.size())) != 0) {
                                return;
                            }
                            const absl::MutexLock lock(&self->color_buffer_mutex_);
                            auto disp_it = self->display_buffers_.find(display_id);
                            if (disp_it != self->display_buffers_.end() &&
                                disp_it->second.cb_handle == cb_handle &&
                                disp_it->second.width == width &&
                                disp_it->second.height == height) {
                                self->frame_buffers_[cb_handle] = std::move(pixels);
                            }
                        };
                        async::EventLoop* qemu_loop = qemu_loop_.load(std::memory_order_acquire);
                        if (qemu_loop != nullptr) {
                            if (qemu_loop->IsOnLoopThread()) {
                                perform_readback();
                            } else {
                                qemu_loop->Post(std::move(perform_readback)).IgnoreError();
                            }
                        } else {
                            LOG(WARNING) << "Skipping virtio-gpu readback for display "
                                         << display_id << ": qemu_loop is not configured";
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
                              receive_buffer_.begin() + sizeof(uint32_t) + payload_size);
    }
}

void MultiDisplayDevice::SendAddDisplayPacket(uint32_t display_id, uint32_t width, uint32_t height,
                                              uint32_t dpi, uint32_t flag) {
    std::string msg(1 + (5 * sizeof(uint32_t)), '\0');
    msg[0] = kCmdAdd;
    auto* data = reinterpret_cast<uint32_t*>(&msg[1]);
    data[0] = display_id;
    data[1] = width;
    data[2] = height;
    data[3] = dpi;
    data[4] = flag;
    SendToGuest(std::move(msg));
}

void MultiDisplayDevice::SendToGuest(std::string message) {
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
    char size_buf[sizeof(uint32_t)];
    // Assuming absl::little_endian
    absl::little_endian::Store32(size_buf, msg.size());

    Socket()->Send(std::string(size_buf, sizeof(size_buf)));
    Socket()->Send(std::move(msg));
}

// --- Registration ---

void MultiDisplayDevice::RegisterDevice(const std::shared_ptr<MultiDisplayDevice>& device,
                                        IConnectorRegistry* registry, async::EventLoop* client_loop,
                                        async::EventLoop* qemu_loop) {
    if (device) {
        device->qemu_loop_.store(qemu_loop, std::memory_order_release);
    }
    registry->RegisterHalDevice("multidisplay", client_loop, qemu_loop,
                                [device](std::string_view /*args*/) { return device; });
}

}  // namespace goldfish::devices::multidisplay

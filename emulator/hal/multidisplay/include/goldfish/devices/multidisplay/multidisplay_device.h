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
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/synchronization/mutex.h"

#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices::multidisplay {

// The external API to send messages to the guest.
// This is thread-safe and can be called from anywhere.

// Returns the color buffer handle for the given display, or 0 if not bound.
// Thread-safe.
uint32_t GetDisplayColorBuffer(uint32_t displayId);
void ReadDisplayColorBuffer(uint32_t colorbuffer, uint8_t* outptr);
void SendToGuest(std::string message);
void SendAddDisplay(uint32_t display_id, uint32_t width, uint32_t height, uint32_t dpi,
                    uint32_t flag);
void SendDelDisplay(uint32_t display_id);
void SendSetDisplay(uint32_t mode_id, uint32_t width, uint32_t height, uint32_t dpi, uint32_t flag);

class MultiDisplayDevice : public HalPlug, public std::enable_shared_from_this<MultiDisplayDevice> {
  public:
    MultiDisplayDevice(async::EventLoop* clientLoop);
    ~MultiDisplayDevice();

    // HalPlug overrides
    void OnConnect();
    void OnClose();
    void OnReceive(std::string_view data);

    // Internal send method
    void SendInternal(std::string message);

    void SendAddDisplayPacket(uint32_t displayId, uint32_t width, uint32_t height, uint32_t dpi,
                              uint32_t flag);

    // Internal helper to retrieve CB safely
    uint32_t GetColorBufferInternal(uint32_t displayId);

    // Internal helper to clear CB on delete
    void RemoveColorBufferInternal(uint32_t displayId);

    void AddDisplayInternal(uint32_t displayId, uint32_t width, uint32_t height, uint32_t dpi,
                            uint32_t flag);

    void ReadDisplayColorBufferInternal(uint32_t colorbuffer, uint8_t* outptr);

    // Static registration function
    static void RegisterDevice(std::shared_ptr<MultiDisplayDevice> device,
                               IConnectorRegistry* registry, async::EventLoop* client_loop,
                               async::EventLoop* qemu_loop);

    struct DisplayInfo {
        uint32_t width;
        uint32_t height;
        uint32_t dpi;
        uint32_t flag;
        uint32_t cb_handle = 0;
    };

    // Command constants matching the guest JNI bridge
    static constexpr uint8_t kCmdAdd = 1;
    static constexpr uint8_t kCmdDel = 2;
    static constexpr uint8_t kCmdQuery = 3;
    static constexpr uint8_t kCmdBind = 4;
    static constexpr uint8_t kCmdSetDisplay = 0x10;

  private:
    async::EventLoop* client_loop_;
    std::vector<uint8_t> receive_buffer_;

    // Protects the map since OnReceive (IO thread) writes and GetDisplayColorBuffer (Render thread)
    // reads.
    mutable absl::Mutex color_buffer_mutex_;

    // Map: Display ID -> Display Info (including Color Buffer Handle)
    std::unordered_map<uint32_t, DisplayInfo> display_buffers_ ABSL_GUARDED_BY(color_buffer_mutex_);

    // Map: Color Buffer Handle -> Framebuffer data
    std::unordered_map<uint32_t, std::vector<uint8_t>> frame_buffers_
            ABSL_GUARDED_BY(color_buffer_mutex_);

    // Helper to frame messages (4-byte length prefix)
    void SendFramed(std::string msg);
};

}  // namespace goldfish::devices::multidisplay

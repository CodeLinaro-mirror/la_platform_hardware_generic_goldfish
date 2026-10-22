/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file HalPlugToIPlugAdapter.h
 * @brief Defines the adapter that bridges the IPlug interface to the HalPlug
 * interface.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices {

/**
 * @class HalPlugToIPlugAdapter
 * @brief An internal implementation of the IPlug interface that acts as an
 * adapter.
 *
 * It wraps a HalPlug and is responsible for marshalling calls from the QEMU
 * event loop to the client event loop where the real HalPlug lives.
 */
class HalPlugToIPlugAdapter : public cable::IPlug,
                              public std::enable_shared_from_this<HalPlugToIPlugAdapter> {
  public:
    /**
     * @brief Constructs a new HalPlugToIPlugAdapter.
     * @param client_loop The event loop on which the HalPlug's methods should be
     * invoked.
     * @param hal_plug The real HalPlug instance to which calls will be
     * forwarded.
     */
    HalPlugToIPlugAdapter(async::EventLoop* client_loop, std::shared_ptr<HalPlug> hal_plug);
    ~HalPlugToIPlugAdapter() override;
    /**
     * @brief Called on the QEMU thread when the connection is established.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onConnect method on the client event loop.
     */
    void OnConnect() override;

    /**
     * @brief Called on the QEMU thread when data is received from the guest.
     *
     * This method is part of the IPlug interface. It coalesces incoming guest
     * chunks into a pre-reserved buffer and marshals delivery to the HalPlug's
     * OnReceive method on the client event loop.
     * @param data A pointer to the received data.
     * @param size The size of the received data.
     * @return Always returns true, as the actual processing is asynchronous.
     */
    bool OnReceive(const void* data, size_t size) override;

    /**
     * @brief Called on the QEMU thread when the connection is unplugged.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onClose method on the client event loop.
     * @return A null SocketPtr, as the connection is terminated.
     */
    cable::SocketPtr OnUnplug() override;

    const std::shared_ptr<HalPlug>& GetHalPlug() const { return hal_plug_; }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[IPlugAdapter: client: %p, halPlug: %v]", client_loop_, *hal_plug_);
    }

  private:
    // Default socket buffer size
    static constexpr size_t kInitialBufferSize = 64 * 1024;

    async::EventLoop* client_loop_;
    const std::shared_ptr<HalPlug> hal_plug_;

    absl::Mutex receive_buffer_mutex_;
    std::string receive_buffer_ ABSL_GUARDED_BY(receive_buffer_mutex_);
    bool has_pending_receive_ ABSL_GUARDED_BY(receive_buffer_mutex_) = false;
    std::string deliver_buffer_;
};

}  // namespace goldfish::devices

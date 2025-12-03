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

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish {
namespace devices {

/**
 * @class HalPlugToIPlugAdapter
 * @brief An internal implementation of the IPlug interface that acts as an
 * adapter.
 *
 * It wraps a HalPlug and is responsible for marshalling calls from the QEMU
 * event loop to the client event loop where the real HalPlug lives.
 */
class HalPlugToIPlugAdapter : public cable::IPlug {
  public:
    /**
     * @brief Constructs a new HalPlugToIPlugAdapter.
     * @param clientLoop The event loop on which the HalPlug's methods should be
     * invoked.
     * @param halPlug The real HalPlug instance to which calls will be
     * forwarded.
     */
    HalPlugToIPlugAdapter(async::EventLoop* clientLoop, std::shared_ptr<HalPlug> halPlug);
    ~HalPlugToIPlugAdapter() override;
    /**
     * @brief Called on the QEMU thread when the connection is established.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onConnect method on the client event loop.
     */
    void onConnect() override;

    /**
     * @brief Called on the QEMU thread when data is received from the guest.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onReceive method on the client event loop.
     * @param data A pointer to the received data.
     * @param size The size of the received data.
     * @return Always returns true, as the actual processing is asynchronous.
     */
    bool onReceive(const void* data, size_t size) override;

    /**
     * @brief Called on the QEMU thread when the connection is unplugged.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onClose method on the client event loop.
     * @return A null SocketPtr, as the connection is terminated.
     */
    cable::SocketPtr onUnplug() override;

    const std::shared_ptr<HalPlug>& getHalPlug() const { return mHalPlug; }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[IPlugAdapter: client: %p, halPlug: %v]", mClientLoop, *mHalPlug);
    }

  private:
    async::EventLoop* mClientLoop;
    std::shared_ptr<HalPlug> mHalPlug;
};

}  // namespace devices
}  // namespace goldfish

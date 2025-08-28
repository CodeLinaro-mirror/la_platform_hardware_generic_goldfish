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
 * @file MarshallingHalSocket.h
 * @brief Defines the socket implementation that marshals calls to the QEMU
 * event loop.
 */
#pragma once

#include <atomic>
#include <functional>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish {
namespace devices {

class HalPlugToIPlugAdapter;
/**
 * @class MarshallingHalSocket
 * @brief An internal implementation of the HalSocket interface.
 *
 * This class is responsible for marshalling calls from a client event loop
 * (e.g., libuv) to the main QEMU event loop where the real ISocket lives.
 * It also takes ownership of the underlying ISocket and manages its lifetime.
 *
 * When the underlying socket is closed it will be replaced by a NullSocket that
 * will send bytes to the void (ie. discarded).
 */
class MarshallingHalSocket : public HalSocket {
  public:
    /**
     * @brief Constructs a new MarshallingHalSocket.
     * @param socket The real ISocket instance that lives on the QEMU thread.
     * @param qemuLoop The QEMU event loop to which calls should be marshalled.
     */
    MarshallingHalSocket(cable::SocketPtr socket, async::EventLoop* qemuLoop);
    ~MarshallingHalSocket() override;

    /**
     * @brief Asynchronously sends data to the guest.
     *
     * This method is part of the HalSocket interface. It marshals the call to
     * the real ISocket's sendAsync method on the QEMU event loop.
     * @param data The data to send.
     */
    void send(std::string data) override;

    /**
     * @brief Asynchronously closes the connection.
     *
     * This method is part of the HalSocket interface. It marshals the call to
     * the real ISocket's unplug method on the QEMU event loop.
     */
    void close() override;

  private:
   friend class HalPlugToIPlugAdapter;
   cable::SocketPtr release();

   cable::SocketPtr mSocket;
   absl::Mutex mSocketMutex;
   async::EventLoop* mQemuLoop;
   std::atomic<bool> mIsClosed{false};
};

}  // namespace devices
}  // namespace goldfish

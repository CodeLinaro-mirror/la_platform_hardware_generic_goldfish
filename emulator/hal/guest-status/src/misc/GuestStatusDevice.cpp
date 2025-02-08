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
#include "android/misc/GuestStatusDevice.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/synchronization/mutex.h"

#include "aemu/base/async/Looper.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/avd.h"
#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/qemud.h"

using android::base::Looper;
using android::base::System;
using android::goldfish::Avd;
using goldfish::devices::PingTopic;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

namespace goldfish::devices::guest_status {

// Example usage for construction
static AndroidGuestStatus createBootCompletedEvent(std::chrono::milliseconds bootTime) {
    return {bootTime};
}
static AndroidGuestStatus createResetEvent() {
    return {AndroidGuestStatus::ResetEvent()};
}
static AndroidGuestStatus createHeartbeatEvent(uint64_t heartbeat) {
    return {heartbeat};
}

class GuestStatusDevice : public IGuestStatusDevice {
  public:
    GuestStatusDevice(SocketPtr socket, RegisterEmulatorReset registerEmulatorReset)
        : mSocket(std::move(socket)),
          mHeartbeat(0),
          mBootTime(std::chrono::milliseconds(0)),
          mResetTimestampMs(std::chrono::milliseconds(0)) {
        VLOG(1) << "GuestStatus device has been created";
        registerEmulatorReset(GuestStatusDevice::QEMUResetHandler, this);
    }

    ~GuestStatusDevice() = default;

    SocketPtr onUnplug() override { return std::move(mSocket); }

    void send(std::string_view msg) {
        goldfish::devices::qemud::sendAsync(msg.data(), msg.size(), *mSocket.get());
    }

    uint64_t heartbeat() const override {
        absl::MutexLock lock(&mStatusMutex);
        return mHeartbeat;
    }

    std::optional<std::chrono::milliseconds> bootTime() const override {
        absl::MutexLock lock(&mStatusMutex);
        if (mBootTime == std::chrono::milliseconds(0)) return std::nullopt;
        return mBootTime;
    }

    bool onReceive(const void* data, size_t size) override {
        std::string_view message(static_cast<const char*>(data), size);
        VLOG(1) << "Received message from guest:" << message;

        if (absl::StartsWith(message, "heartbeat")) {
            uint64_t heartbeat = 0;
            {
                absl::MutexLock lock(&mStatusMutex);
                heartbeat = ++mHeartbeat;
            }
            VLOG(1) << "Heartbeat: " << heartbeat;
            fireEvent(createHeartbeatEvent(heartbeat));
        } else if (absl::StartsWith(message, "bootcomplete")) {
            std::chrono::milliseconds bootTime;
            {
                absl::MutexLock lock(&mStatusMutex);
                bootTime = uptime() - mResetTimestampMs;
                mBootTime = bootTime;
            }
            fireEvent(createBootCompletedEvent(bootTime));
            VLOG(1) << "Completed booting in: " << bootTime.count() << " ms.";
        } else {
            VLOG(1) << "Ignoring unknown message from guest (" << message.size() << "):" << message;
        }

        send("KO");
        return true;
    }

  private:
    static void QEMUResetHandler(void* opaque) {
        auto device = static_cast<GuestStatusDevice*>(opaque);
        device->handleResetEvent();
    }

    void handleResetEvent() {
        {
            absl::MutexLock lock(&mStatusMutex);
            mBootTime = std::chrono::milliseconds{0};
            mResetTimestampMs = uptime();
        }
        fireEvent(createResetEvent());
    }

    std::chrono::milliseconds uptime() {
        return std::chrono::milliseconds(System::get()->getProcessTimes().wallClockMs);
    }

    SocketPtr mSocket;
    uint64_t mHeartbeat ABSL_GUARDED_BY(mStatusMutex);
    std::chrono::milliseconds mBootTime ABSL_GUARDED_BY(mStatusMutex);
    std::chrono::milliseconds mResetTimestampMs ABSL_GUARDED_BY(mStatusMutex);

    mutable absl::Mutex mStatusMutex;  // protects mHeartbeat, mBootTime, mResetTimestampMs
};

void IGuestStatusDevice::registerDevice(IConnectorRegistry* registry,
                                        RegisterEmulatorReset registerEmulatorReset) {
    registry->registerDevice(std::string(IGuestStatusDevice::serviceName),
                             [registerEmulatorReset = std::move(registerEmulatorReset)](
                                     SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                                     std::string_view args) {
                                 return std::make_shared<GuestStatusDevice>(std::move(socket),
                                                                            registerEmulatorReset);
                             });
}

}  // namespace goldfish::devices::guest_status
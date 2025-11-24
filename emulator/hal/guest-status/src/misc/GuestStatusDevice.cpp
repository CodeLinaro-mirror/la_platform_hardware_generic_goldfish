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

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

#include "android/base/system/System.h"
#include "android/goldfish/vm/VmInterface.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/qemud.h"
#include "goldfish/hal/common/emulator_reset.h"

using android::base::System;

namespace goldfish::devices::guest_status {

static std::chrono::milliseconds s_uptime{};

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
    GuestStatusDevice(EmulatorResetCallbacks resetCallbacks, async::EventLoop* qemuLoop,
                      int quitAfterBootTimeoutSeconds)
            : mResetCallbacks(resetCallbacks)
            , mQemuLoop(qemuLoop)
            , mQuitAfterBootTimeoutSeconds(quitAfterBootTimeoutSeconds)
            , mHeartbeat(0)
            , mBootTime(std::chrono::milliseconds(0))
            , mResetTimestampMs(s_uptime) {
        VLOG(1) << "GuestStatus device has been created";
        if (mResetCallbacks.do_register) {
            mResetCallbacks.do_register(GuestStatusDevice::QEMUResetHandler, this);
        }
    }

    void send(std::string msg) {
        char sizeBuf[sizeof(uint32_t)];
        absl::little_endian::Store32(sizeBuf, msg.size());
        socket()->send(std::string(sizeBuf, sizeof(sizeBuf)));
        socket()->send(std::move(msg));
    }

    uint64_t heartbeat() const override {
        return mHeartbeat;
    }

    std::optional<std::chrono::milliseconds> bootTime() const override {
        if (mBootTime == std::chrono::milliseconds(0)) return std::nullopt;
        return mBootTime;
    }

    void onConnect() override { VLOG(1) << "Guest status device has been connected"; }
    void onClose() override { VLOG(1) << "Guest status device has been disconnected"; }

    void onReceive(const std::string_view data) override {
        mReceiveData.insert(mReceiveData.end(), data.begin(), data.end());

        while (true) {
            if (mReceiveData.size() < sizeof(uint32_t)) {
                return;
            }

            const uint32_t msgSize = absl::little_endian::Load32(mReceiveData.data());
            if (mReceiveData.size() < (sizeof(uint32_t) + msgSize)) {
                return;
            }

            onReceiveMsg(std::string_view(&mReceiveData[sizeof(uint32_t)], msgSize));

            mReceiveData.erase(mReceiveData.begin(),
                            mReceiveData.begin() + sizeof(uint32_t) + msgSize);
        }
    }

    void onReceiveMsg(const std::string_view message) {
        using namespace std::literals;

        VLOG(2) << "Received message from guest: '" << message << "'";

        // see sendMessage in device/generic/goldfish/qemu-props/qemu-props.cpp

        bool ok = true;
        if (message == "heartbeat\0"sv) {
            VLOG(2) << "Heartbeat: " << message;
            onReceiveHeartbeat();
        } else if (message == "bootcomplete\0"sv) {
            onReceiveBootcomplete();
        } else {
            VLOG(1) << "Ignoring unknown message from guest (" << message.size() << "):" << message;
            ok = false;
        }

        send(ok ? "OK"s : "KO"s);
    }

    void onReceiveHeartbeat() {
        fireEvent(createHeartbeatEvent(++mHeartbeat));
    }

    void onReceiveBootcomplete() {
        const std::chrono::milliseconds bootTime = uptime() - mResetTimestampMs;
        mBootTime = bootTime;
        fireEvent(createBootCompletedEvent(bootTime));

        // use WARNING, otherwise, logger does no flush and we don't know
        // it boot completes in timely manner
        LOG(WARNING) << "Boot completed in " << bootTime.count() << " ms";

        if (mQuitAfterBootTimeoutSeconds > 0) {
            LOG(WARNING) << "Shutting down guest due to boot complete";
            // onReceive is not called on Qemu thread - schedule shutdown from there to be safe.
            (void)mQemuLoop->post([] () {
                android::goldfish::VmOperations::qemuVmOperations()->systemShutdownRequest(android::goldfish::QemuShutdownCause::GuestShutdown);
            });
        }
    }

    void unregisterResetHandler() {
        if (mResetCallbacks.do_unregister) {
                    mResetCallbacks.do_unregister(GuestStatusDevice::QEMUResetHandler, this);
        }
    }
  private:
    static void QEMUResetHandler(void* opaque) {
        auto device = static_cast<GuestStatusDevice*>(opaque);
        device->handleResetEvent();
    }

    void handleResetEvent() {
        mBootTime = std::chrono::milliseconds{0};
        s_uptime = uptime();
        fireEvent(createResetEvent());
    }

    std::chrono::milliseconds uptime() {
        return std::chrono::milliseconds(System::get()->getProcessTimes().wallClockMs);
    }

    EmulatorResetCallbacks mResetCallbacks;
    async::EventLoop *mQemuLoop;
    std::vector<char> mReceiveData;
    const int mQuitAfterBootTimeoutSeconds;
    uint64_t mHeartbeat;
    std::chrono::milliseconds mBootTime;
    std::chrono::milliseconds mResetTimestampMs;
};

// TODO: b/456020509: do something better here
static    std::shared_ptr<GuestStatusDevice> s_GuestStatusDevice;

bool IGuestStatusDevice::isBootCompleted() {
    if (s_GuestStatusDevice) {
        return s_GuestStatusDevice->hasBooted();
    }
    return false;
}

void IGuestStatusDevice::registerDevice(IConnectorRegistry* registry,
                                        EmulatorResetCallbacks resetCallbacks,
                                        EventLoop* clientLoop, EventLoop* qemuLoop,
                                        int quitAfterBootTimeoutSeconds) {
    registry->registerHalDevice(std::string(IGuestStatusDevice::serviceName), clientLoop, qemuLoop,
                                [resetCallbacks, qemuLoop, quitAfterBootTimeoutSeconds] {
                if (s_GuestStatusDevice) {
                    s_GuestStatusDevice->unregisterResetHandler();
                }
                s_GuestStatusDevice =
                std::make_shared<GuestStatusDevice>(resetCallbacks, qemuLoop, quitAfterBootTimeoutSeconds);
                return s_GuestStatusDevice; });
}

}  // namespace goldfish::devices::guest_status

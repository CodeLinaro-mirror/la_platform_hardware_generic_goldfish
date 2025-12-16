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
#include "goldfish/devices/guest_status/guest_status_device.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"

#include "android/base/system.h"
#include "android/goldfish/vm_interface.h"
#include "goldfish/devices/qemud/qemud.h"

namespace goldfish::devices::guest_status {

using avd_universe::guest_status::ObservableCounter;
using avd_universe::guest_status::ObservableTimestamp;

using android::base::eventing::ScopedEventCallback;

using HeartbeatSubscription =
        std::unique_ptr<ScopedEventCallback<ObservableCounter, ObservableCounter::EventType>>;

using TimestampSubscription =
        std::unique_ptr<ScopedEventCallback<ObservableTimestamp, ObservableTimestamp::EventType>>;

void emptyUnregisterEmulatorReset(EmulatorResetCallbacks::QEMUResetHandler*, void*) {}

class GuestStatusDevice : public IGuestStatusDevice,
                          std::enable_shared_from_this<GuestStatusDevice> {
  public:
    GuestStatusDevice(GuestStatus& guestStatus, const EmulatorResetCallbacks resetCallbacks,
                      async::EventLoop* qemuLoop, const int quitAfterBootTimeoutSeconds)
            : mGuestStatus(guestStatus)
            , mQemuLoop(qemuLoop)
            , mQuitAfterBootTimeoutSeconds(quitAfterBootTimeoutSeconds) {
        VLOG(1) << "GuestStatus device has been created";
        if (resetCallbacks.do_register) {
            DCHECK(resetCallbacks.do_unregister);
            resetCallbacks.do_register(GuestStatusDevice::QEMUResetHandler, this);
            mUnregisterEmulatorReset = resetCallbacks.do_unregister;
        } else {
            mUnregisterEmulatorReset = &emptyUnregisterEmulatorReset;
        }
    }

    void onConnect() override { VLOG(1) << "Guest status device has been connected"; }

    void onClose() override {
        VLOG(1) << "Guest status device has been disconnected";

        // TODO shared_from_this() throws here as this no longer has any associated shared_ptr.
        // Presumably it's being destroyed?
        /*(void)mQemuLoop->post([self = shared_from_this()]() {
            self->mUnregisterEmulatorReset(GuestStatusDevice::QEMUResetHandler, self.get());
        });*/
    }

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

  private:
    /*
     * This magic string MUST be printed: this is how the tools detect
     * that the system image booted.
     *
     * Use `WARNING`, otherwise, logger does no flush and we
     * don't know it boot completes in timely manner.
     */
    static void notifyToolsBootcomplete(const size_t durationMs) {
        LOG(WARNING) << "Boot completed in " << durationMs << " ms";
    }

    void send(std::string msg) {
        char sizeBuf[sizeof(uint32_t)];
        absl::little_endian::Store32(sizeBuf, msg.size());
        socket()->send(std::string(sizeBuf, sizeof(sizeBuf)));
        socket()->send(std::move(msg));
    }

    void onReceiveMsg(const std::string_view message) {
        using namespace std::literals;

        VLOG(2) << "Received message from guest: '" << message << "'";

        bool ok = true;
        // see sendMessage in device/generic/goldfish/qemu-props/qemu-props.cpp
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

    void onReceiveHeartbeat() { mGuestStatus.heartbeat.setValue(++mHeartbeatCounter); }

    void onReceiveBootcomplete() {
        absl::Time now = wallClock();
        mGuestStatus.bootcomplete.setValue(now);
        notifyToolsBootcomplete(
                size_t(absl::ToInt64Milliseconds(now - mGuestStatus.reset.getValue())));

        if (mQuitAfterBootTimeoutSeconds > 0) {
            LOG(WARNING) << "Shutting down guest due to boot complete";
            // onReceive is not called on Qemu thread - schedule shutdown from there to be safe.
            (void)mQemuLoop->post([]() {
                android::goldfish::VmOperations::qemuVmOperations()->systemShutdownRequest(
                        android::goldfish::QemuShutdownCause::GuestShutdown);
            });
        }
    }

    void handleResetEvent() {
        mGuestStatus.bootcomplete.setValue(absl::UnixEpoch());
        mGuestStatus.reset.setValue(wallClock());
    }

    static absl::Time wallClock() {
        return absl::UnixEpoch() +
               absl::Milliseconds(android::base::System::get()->getProcessTimes().wallClockMs);
    }

    static void QEMUResetHandler(void* opaque) {
        static_cast<GuestStatusDevice*>(opaque)->handleResetEvent();
    }

    GuestStatus& mGuestStatus;
    async::EventLoop* const mQemuLoop;
    EmulatorResetCallbacks::UnregisterEmulatorReset mUnregisterEmulatorReset;
    std::vector<char> mReceiveData;
    ObservableCounter::EventType mHeartbeatCounter = 0;
    const int mQuitAfterBootTimeoutSeconds;
};

void IGuestStatusDevice::registerDevice(GuestStatus* guestStatus, IConnectorRegistry* registry,
                                        EmulatorResetCallbacks resetCallbacks,
                                        EventLoop* clientLoop, EventLoop* qemuLoop,
                                        int quitAfterBootTimeoutSeconds) {
    registry->registerHalDevice(
            std::string(IGuestStatusDevice::serviceName), clientLoop, qemuLoop,
            [guestStatus, resetCallbacks, qemuLoop, quitAfterBootTimeoutSeconds] {
                return std::make_shared<GuestStatusDevice>(*guestStatus, resetCallbacks, qemuLoop,
                                                           quitAfterBootTimeoutSeconds);
            });
}

}  // namespace goldfish::devices::guest_status

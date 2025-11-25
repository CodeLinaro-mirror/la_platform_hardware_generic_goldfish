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
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include "aemu/base/events/EventSources.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/hal/common/emulator_reset.h"

namespace goldfish::devices::guest_status {

using android::base::eventing::CallbackEventSource;
using goldfish::async::EventLoop;
using namespace std::string_view_literals;

/**
 * @brief Represents the status of the Android guest.
 *
 * This struct encapsulates various status events from the Android guest,
 * including boot completion, reset events, and heartbeat signals.  It uses
 * a variant to store the event data associated with each event type.
 *
 * @note It is possible to receive multiple consecutive `ResetEvent`s. This can
 * happen due to the concurrent nature of guest and emulator operations. For
 * example, a `ResetEvent` is triggered by a QEMU reset and also when the guest
 * closes the communication channel. Listeners should be designed to handle
 * repeated reset events gracefully.
 */
struct AndroidGuestStatus {
    using BootCompletedEvent = std::chrono::milliseconds;
    using HeartbeatEvent = uint64_t;
    struct ResetEvent {};

    std::variant<BootCompletedEvent, ResetEvent, HeartbeatEvent> eventData;

    // Helper functions to check event type and access data safely
    bool isBootCompletedEvent() const { return std::get_if<BootCompletedEvent>(&eventData); }
    bool isResetEvent() const { return std::get_if<ResetEvent>(&eventData); }
    bool isHeartbeatEvent() const { return std::get_if<HeartbeatEvent>(&eventData); }

    std::chrono::milliseconds bootTime() const {
        if (isBootCompletedEvent()) {
            return std::get<BootCompletedEvent>(eventData);
        }
        return std::chrono::milliseconds(0);
    }

    uint64_t heartbeat() const {
        if (isHeartbeatEvent()) {
            return std::get<HeartbeatEvent>(eventData);
        }
        return 0;
    }
};

/**
 * @brief Provides status information from the Android guest.
 *
 * This class delivers status updates from the Android guest, such as
 * boot completion, reset events, and heartbeat signals. Clients can register
 * callbacks or event listeners to receive these updates. The guest side
 * implementation lives in device/generic/goldfish/qemu-props/qemu-props.cpp
 *
 * Example usage:
 *
 * ```c++
 * class MyGuestStatusListener : public EventListener<AndroidGuestStatus> {
 * public:
 *     void eventArrived(const AndroidGuestStatus& status) override {
 *         if (status.isBootCompletedEvent()) {
 *             printf("Boot completed in %lld ms\n", status.bootTime().count());
 *         } else if (status.isHeartbeatEvent()) {
 *             printf("Heartbeat: %llu\n", status.heartbeat());
 *         } else if (status.isResetEvent()){
 *             printf("Guest reset\n");
 *         }
 *     }
 * };
 *
 * MyGuestStatusListener listener;
 * guestStatusDevice->addListener(&listener);
 * // ... later
 * guestStatusDevice->removeListener(&listener);
 *
 * // Alternatively, using callbacks:
 * auto callbackId = guestStatusDevice->addCallback(
 *     [](const AndroidGuestStatus& status) {
 *         // Process status updates.
 *     });
 * // ... later
 * guestStatusDevice->removeCallback(callbackId);
 *
 * ```
 */
class IGuestStatusDevice : public HalPlug, public CallbackEventSource<AndroidGuestStatus> {
  public:
    static constexpr std::string_view serviceName = "QemuMiscPipe"sv;

    /**
     * @brief Retrieves the current heartbeat count.
     *
     * This represents the number of heartbeat messages received from the guest.
     *
     * @return The number of received heartbeats.
     */
    virtual uint64_t heartbeat() const = 0;

    /**
     * @brief Retrieves the boot time of the guest.
     *
     * Returns the time taken for the guest to boot, if available.  This value
     * is set after the guest signals boot completion.
     *
     * @return An optional containing the boot time if the guest has booted,
     *         std::nullopt otherwise.
     */
    virtual std::optional<std::chrono::milliseconds> bootTime() const = 0;

    /**
     * @brief Checks if the guest has finished booting.
     *
     * @return True if the guest has booted, false otherwise.
     */
    bool hasBooted() const { return bootTime().has_value(); };

    /**
     * @brief Registers the guest status device with the connector registry.
     *
     * This function registers the guest status device with the provided
     * `IConnectorRegistry` instance, making it available for connection
     * through the qemud pipe.  The provided `EmulatorResetCallbacks` struct allows
     * the device to register and unregister a callback for emulator reset events.
     *
     * @param registry The `IConnectorRegistry` instance to register with.
     * @param resetCallbacks The struct containing register/unregister functions.
     */
    static bool isBootCompleted();
    static void registerDevice(IConnectorRegistry* registry, EmulatorResetCallbacks resetCallbacks,
                               EventLoop* clientLoop, EventLoop* qemuLoop,
                               int quitAfterBootTimeoutSeconds);
};
}  // namespace goldfish::devices::guest_status

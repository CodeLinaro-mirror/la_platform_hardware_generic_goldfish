#pragma once

#include <memory>

#include "android/misc/GuestStatusDevice.h"

namespace goldfish::devices::guest_status {

/**
 * A fake guest status device for testing.
 */
class FakeGuestDevice : public IGuestStatusDevice,
                        public std::enable_shared_from_this<FakeGuestDevice> {
  public:
    // IGuestStatusDevice implementation
    /**
     * @brief Returns a constant heartbeat value.
     * @return Always returns 0.
     */
    uint64_t heartbeat() const override { return mHeartbeat; }
    void setHeartbeat(uint64_t heartbeat) { mHeartbeat = heartbeat; }

    /**
     * @brief Returns the simulated boot time.
     * @return An optional containing the boot time if `sendBootCompleted` has been called,
     *         otherwise an empty optional.
     */
    std::optional<std::chrono::milliseconds> bootTime() const override { return mBootTime; }
    void setBootTime(std::optional<std::chrono::milliseconds> bootTime) { mBootTime = bootTime; }

    void onConnect() override {}
    void onClose() override {}
    void onReceive(std::string_view data) override {}
    /**
     * @brief Simulates the boot completed event.
     *
     * This method sets a mock boot time and fires a BootCompletedEvent,
     * simulating the behavior of a guest that has finished booting.
     */
    void sendBootCompleted() {
        mBootTime = std::chrono::milliseconds(1234);
        fireEvent(AndroidGuestStatus{AndroidGuestStatus::BootCompletedEvent{*mBootTime}});
    }

  private:
    uint64_t mHeartbeat = 0;
    std::optional<std::chrono::milliseconds> mBootTime;
};

}  // namespace goldfish::devices::guest_status

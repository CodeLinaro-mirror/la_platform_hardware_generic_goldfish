// Copyright (C) 2025 The Android Open Source Project
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

#include <sys/types.h>

#include <atomic>
#include <memory>

#include "absl/time/time.h"

namespace android::base {

/**
 * @brief Defines the different time domains available in the emulator.
 * This is a C++ native enum, completely decoupled from QEMU's C headers.
 */
enum class ClockType : uint8_t {
    /**
     * @brief The virtual (guest) clock.
     * This clock only advances when the guest is running and is paused when
     * the guest is suspended. It is the appropriate source for guest-visible
     * events. Corresponds to `QEMU_CLOCK_VIRTUAL`.
     */
    kVirtual,

    /**
     * @brief The host system's wall-clock time.
     * This clock advances regardless of the guest's state and reflects the
     * real-world time as seen by the host machine. It is suitable for host-side
     * timeouts or logging. Corresponds to `QEMU_CLOCK_HOST`.
     */
    kHost,

    /**
     * @brief The host's real-time clock.
     * Similar to `Host`, but this clock is not affected by NTP or manual time
     * adjustments on the host. It should be used for high-precision timers
     * that must not be affected by system time changes. Corresponds to
     * `QEMU_CLOCK_REALTIME`.
     */
    kRealtime
};

/**
 * @brief An interface for a clock that provides access to different
 * time domains within the emulator.
 */
class IClock {
  public:
    virtual ~IClock() = default;

    /**
     * @brief Gets the current time for a specific clock type.
     * @param type The clock domain to query.
     * @return The current time as an absl::Time object.
     */
    virtual absl::Time Now(ClockType type) const = 0;

    // --- Static Accessor Methods ---

    /**
     * @brief Sets the global clock instance. This should be called once
     * during application startup.
     * @param clock A unique_ptr to the clock implementation.
     */
    static void Set(std::unique_ptr<IClock> clock);

    /**
     * @brief Gets a reference to the global clock instance.
     *
     * If IClock::set() has not been called, this method will initialize
     * a default IClock implementation using AbseilClock. In this fallback
     * case, all ClockType values will return the host system time based
     * on absl::Now(). A warning will be logged on the first fallback.
     *
     * @return A reference to the IClock.
     */
    static IClock& Get();

    // --- Static Convenience Methods ---

    static absl::Time VirtualNow() { return Get().Now(ClockType::kVirtual); }
    static absl::Time HostNow() { return Get().Now(ClockType::kHost); }
    static absl::Time RealtimeNow() { return Get().Now(ClockType::kRealtime); }

  private:
    static std::atomic<IClock*> s_instance;
    static std::unique_ptr<IClock> s_owned_instance;
};

}  // namespace android::base

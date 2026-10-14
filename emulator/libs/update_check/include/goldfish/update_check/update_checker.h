// Copyright 2026 The Android Open Source Project
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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "goldfish/network/http_client.h"
#include "goldfish/update_check/repository_manifest.h"

namespace goldfish::update_check {

inline constexpr std::string_view kUpdateCheckIniFileName = "emu-update-last-check.ini";

struct UpdateCheckOptions {
    std::filesystem::path user_directory;
    std::string base_url;
    std::string package_prefix;
    std::string package_display_name;
    std::string emulator_version;
    std::string build_id;
    std::string platform;
    std::string target_cpu;
    std::string core_version;
    std::string user_id;
    UpdateChannel channel = UpdateChannel::kStable;
    absl::Duration interval;
    absl::Duration http_timeout;
};

// RAII handle for the one-shot background update check thread.
// During a normal emulator session, the background check completes on its own;
// if the handle is destroyed while the HTTP request is still in flight,
// Reset() signals cancellation and joins the thread. Call Wait() to wait for
// normal completion without signalling cancellation.
class ScopedUpdateCheck {
  public:
    ScopedUpdateCheck() = default;
    explicit ScopedUpdateCheck(std::jthread thread) : thread_(std::move(thread)) {}
    ScopedUpdateCheck(const ScopedUpdateCheck&) = delete;
    ScopedUpdateCheck& operator=(const ScopedUpdateCheck&) = delete;
    ScopedUpdateCheck(ScopedUpdateCheck&&) noexcept = default;
    ScopedUpdateCheck& operator=(ScopedUpdateCheck&& other) noexcept {
        if (this != &other) {
            Reset();
            thread_ = std::move(other.thread_);
        }
        return *this;
    }
    ~ScopedUpdateCheck() { Reset(); }

    // Waits for the background update check to finish without cancelling it.
    void Wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    void Reset() {
        thread_.request_stop();
        Wait();
    }

    std::jthread thread_;
};

// Checks for a newer package in the SDK repository manifest.
// Returns the formatted update notice if a newer version is available,
// or std::nullopt if up to date, throttled, or running a non-release build.
absl::StatusOr<std::optional<std::string>> CheckForUpdate(
        const UpdateCheckOptions& options, goldfish::network::HttpClient& http_client,
        absl::Time now);

// Runs CheckForUpdate asynchronously on a background thread and logs a warning
// if an update notice is returned. The returned ScopedUpdateCheck joins the
// thread on destruction.
ScopedUpdateCheck StartAsyncUpdateCheck(UpdateCheckOptions options);

}  // namespace goldfish::update_check

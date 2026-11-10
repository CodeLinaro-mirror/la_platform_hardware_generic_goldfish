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

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "android/process/process.h"

namespace goldfish::adb::testing {

// Generates a 2048-bit RSA private key in PEM format at `file` with 0600 permissions.
bool GenerateTestAdbPrivateKey(const std::filesystem::path& file);

// Creates an executable stub `adb` (or `adb.exe` on Windows) inside `dir` and returns its path.
std::filesystem::path CreateStubAdbExecutable(const std::filesystem::path& dir);

// RAII override for `android::base::Command::SetTestProcessFactory`.
class ScopedTestProcessFactory {
  public:
    using Handler = std::function<android::base::ProcessExitCode(
            const android::base::CommandArguments&, std::string* std_out, std::string* std_err)>;

    explicit ScopedTestProcessFactory(Handler handler);
    ~ScopedTestProcessFactory();

    ScopedTestProcessFactory(const ScopedTestProcessFactory&) = delete;
    ScopedTestProcessFactory& operator=(const ScopedTestProcessFactory&) = delete;
};

// RAII fake for `adb keygen <out>` invocations through `android::base::Command`.
class ScopedFakeAdbKeygen {
  public:
    explicit ScopedFakeAdbKeygen(
            std::chrono::milliseconds delay = std::chrono::milliseconds::zero());

    ScopedFakeAdbKeygen(const ScopedFakeAdbKeygen&) = delete;
    ScopedFakeAdbKeygen& operator=(const ScopedFakeAdbKeygen&) = delete;

    int invocations() const { return invocations_.load(std::memory_order_acquire); }
    android::base::CommandArguments last_args() const ABSL_LOCKS_EXCLUDED(mu_);

  private:
    std::atomic<int> invocations_{0};
    mutable absl::Mutex mu_;
    android::base::CommandArguments last_args_ ABSL_GUARDED_BY(mu_);
    ScopedTestProcessFactory factory_;
};

}  // namespace goldfish::adb::testing

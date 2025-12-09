// Copyright (C) 2015 The Android Open Source Project
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

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

namespace android::base {

#ifdef _WIN32
// On Windows, this is a no-op.
class ScopedNoSigAlarm {
  public:
    ScopedNoSigAlarm(int /* signal */, int /* how */) {};
    ~ScopedNoSigAlarm() = default;

    // Disallow copy and assign.
    ScopedNoSigAlarm(const ScopedNoSigAlarm&) = delete;
    ScopedNoSigAlarm& operator=(const ScopedNoSigAlarm&) = delete;

    //  Disallow move and assign
    ScopedNoSigAlarm(const ScopedNoSigAlarm&&) noexcept = delete;
    ScopedNoSigAlarm& operator=(const ScopedNoSigAlarm&&) noexcept = delete;
};

#else   // !_WIN32

// On non-Windows platforms, this class masks the given signal
// using the method in how.
class ScopedNoSigAlarm {
  public:
    ScopedNoSigAlarm(int signal = SIGALRM, int how = SIG_BLOCK) {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, signal);
        pthread_sigmask(how, &set, &mOldSet);
    }

    ~ScopedNoSigAlarm() { pthread_sigmask(SIG_SETMASK, &mOldSet, nullptr); }

    // Disallow copy and assign.
    ScopedNoSigAlarm(const ScopedNoSigAlarm&) = delete;
    ScopedNoSigAlarm& operator=(const ScopedNoSigAlarm&) = delete;

    //  Disallow move and assign
    ScopedNoSigAlarm(const ScopedNoSigAlarm&&) noexcept = delete;
    ScopedNoSigAlarm& operator=(const ScopedNoSigAlarm&&) noexcept = delete;

  private:
    sigset_t mOldSet;
};
#endif  // !_WIN32

}  // namespace android::base

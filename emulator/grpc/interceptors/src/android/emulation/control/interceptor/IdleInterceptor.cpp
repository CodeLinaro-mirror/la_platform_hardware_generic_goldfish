// Copyright (C) 2019 The Android Open Source Project
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
#include "android/emulation/control/interceptor/IdleInterceptor.h"

#include <functional>

#include "absl/log/log.h"
#include "absl/time/time.h"

#include "aemu/base/process/Process.h"
#include "android/base/system/clock.h"
#include "goldfish/async/event_loop.h"

namespace android {
namespace control {
namespace interceptor {

using namespace grpc::experimental;
using android::base::IClock;
using android::base::Process;
using goldfish::async::EventLoop;

IdleInterceptor::IdleInterceptor(std::chrono::seconds timeout,
                                 std::atomic<uint64_t>* terminationUnixTime,
                                 std::atomic<uint64_t>* activeRequests)
        : mTimeout(timeout)
        , mTerminationUnixTime(terminationUnixTime)
        , mActiveRequests(activeRequests) {}

IdleInterceptor::~IdleInterceptor() {
    auto idleTime = absl::ToUnixSeconds(IClock::host_now() + absl::Seconds(mTimeout.count()));
    mTerminationUnixTime->store(idleTime);
    mActiveRequests->fetch_sub(1);
}

void IdleInterceptor::Intercept(InterceptorBatchMethods* methods) {
    methods->Proceed();
}

IdleInterceptorFactory::IdleInterceptorFactory(std::chrono::seconds timeout, EventLoop* eventLoop)
        : mTimeout(timeout)
        , mTerminationUnixTime(
                  absl::ToUnixSeconds(IClock::host_now() + absl::Seconds(timeout.count()))) {
    mTimeoutChecker = eventLoop->scheduleRepeating(
            [this]() { checkIdleTimeout(); }, std::chrono::milliseconds(mTimeout),
            std::chrono::milliseconds(mTimeout));
}

Interceptor* IdleInterceptorFactory::CreateServerInterceptor(ServerRpcInfo* info) {
    mActiveRequests++;
    return new IdleInterceptor(mTimeout, &mTerminationUnixTime, &mActiveRequests);
}

bool IdleInterceptorFactory::checkIdleTimeout() {
    auto epoch = absl::ToUnixSeconds(IClock::host_now());
    if (mActiveRequests > 0 || epoch < mTerminationUnixTime) return true;

    LOG(WARNING) << "Idled to long, shutting down. " << epoch << " > " << mTerminationUnixTime;
    if (mShutdownAttempt == 0) {
        LOG(WARNING) << "Trying nicely is not yet implemented..";
    } else {
        LOG(INFO) << "Terminating the emulator.";
        auto me = android::base::Process::me();
        if (me) {
            me->terminate();
        }
    }

    mShutdownAttempt++;
    return true;
}

}  // namespace interceptor
}  // namespace control
}  // namespace android

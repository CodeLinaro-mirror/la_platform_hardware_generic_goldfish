// Copyright (C) 2020 The Android Open Source Project
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
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include "goldfish/async/event_loop.h"

namespace android {
namespace control {
namespace interceptor {

using namespace grpc::experimental;
using goldfish::async::EventLoop;

// An IdleInterceptor can be installed if you wish to terminate the emulator
// when there is no gRPC activity within the given timeout.
class IdleInterceptor : public grpc::experimental::Interceptor {
  public:
    IdleInterceptor(std::chrono::seconds timeout, std::atomic<uint64_t>* terminationUnixTime,
                    std::atomic<uint64_t>* activeRequests);
    ~IdleInterceptor();
    virtual void Intercept(InterceptorBatchMethods* methods) override;

  private:
    std::chrono::seconds mTimeout;
    std::atomic<uint64_t>* mTerminationUnixTime;
    std::atomic<uint64_t>* mActiveRequests;
};

// The factory class that needs to be registered with the gRPC server/client.
//
// The interceptor will schedule a check every timeout seconds to see if any
// gRPC activity took place. If no activity took place the emulator will be
// shutdown in an orderly fashion.
class IdleInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface {
  public:
    IdleInterceptorFactory(std::chrono::seconds timeout, EventLoop* eventLoop);
    virtual ~IdleInterceptorFactory() = default;
    virtual Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override;
    bool checkIdleTimeout();

  private:
    int mShutdownAttempt{0};
    std::chrono::seconds mTimeout;
    std::atomic<uint64_t> mTerminationUnixTime;
    std::atomic<uint64_t> mActiveRequests;
    std::shared_ptr<EventLoop::Timer> mTimeoutChecker;
};  // namespace interceptor

}  // namespace interceptor
}  // namespace control
}  // namespace android

// Copyright (C) 2026 The Android Open Source Project
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

#include <functional>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/emulator_grpc_client.h"

namespace goldfish::telnet {

class NetsimConnectionProvider {
  public:
    NetsimConnectionProvider() = delete;

    using ClientFactory = std::function<absl::StatusOr<
            std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient>>()>;
    using AvdNameProvider = std::function<std::string()>;

    static void Register(ClientFactory client_factory, AvdNameProvider avd_name_provider) {
        absl::MutexLock lock(&Mutex());
        s_client_factory = std::move(client_factory);
        s_avd_name_provider = std::move(avd_name_provider);
    }

    static void Reset() {
        absl::MutexLock lock(&Mutex());
        s_client_factory = nullptr;
        s_avd_name_provider = nullptr;
    }

    static absl::StatusOr<std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient>>
    GetClient() {
        ClientFactory factory;
        {
            absl::MutexLock lock(&Mutex());
            if (!s_client_factory) {
                return absl::FailedPreconditionError("Netsim client factory not registered");
            }
            factory = s_client_factory;
        }
        return factory();
    }

    static std::string GetAvdName() {
        absl::MutexLock lock(&Mutex());
        if (!s_avd_name_provider) {
            return "";
        }
        return s_avd_name_provider();
    }

  private:
    static absl::Mutex& Mutex() {
        static absl::Mutex mutex;
        return mutex;
    }
    inline static ClientFactory s_client_factory;
    inline static AvdNameProvider s_avd_name_provider;
};

}  // namespace goldfish::telnet

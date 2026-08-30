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

#include <grpcpp/grpcpp.h>

#include <memory>
#include <vector>

#include "android/goldfish/vm_interface.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/avd_info/avd_info.h"

namespace goldfish::grpc::v2 {

/**
 * @brief Creates and bundles the AEMU v2 microservice implementations.
 */
std::vector<std::shared_ptr<::grpc::Service>> CreateV2Services(
        ::goldfish::avd_info::AvdUniverse& avd_universe,
        ::android::goldfish::VmOperations* vm_operations = nullptr,
        ::goldfish::async::EventLoop* event_loop = nullptr);

}  // namespace goldfish::grpc::v2

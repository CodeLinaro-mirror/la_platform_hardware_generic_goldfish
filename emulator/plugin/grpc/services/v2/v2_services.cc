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

#include "goldfish/grpc/v2/v2_services.h"

#include "discovery/capabilities_service_impl.h"

namespace goldfish::grpc::v2 {

using ::android::goldfish::VmOperations;
using ::goldfish::async::EventLoop;
using ::goldfish::avd_info::AvdUniverse;
using ::grpc::Service;

std::vector<std::shared_ptr<Service>> CreateV2Services(AvdUniverse& avd_universe,
                                                       VmOperations* /*vm_operations*/,
                                                       EventLoop* /*event_loop*/) {
    std::vector<std::shared_ptr<Service>> services;

    services.emplace_back(std::make_shared<CapabilitiesServiceImpl>(avd_universe));

    return services;
}

}  // namespace goldfish::grpc::v2

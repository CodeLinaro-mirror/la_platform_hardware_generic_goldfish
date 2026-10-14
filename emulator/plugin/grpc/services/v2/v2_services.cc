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
#include "environment/sensors_service_impl.h"
#include "hypervisor/hypervisor_service_impl.h"
#include "operations/operations_service_impl.h"
#include "system/system_service_impl.h"

namespace goldfish::grpc::v2 {

using ::android::goldfish::VmOperations;
using ::goldfish::async::EventLoop;
using ::goldfish::avd::AvdUniverse;
using ::grpc::Service;

std::vector<std::shared_ptr<Service>> CreateV2Services(AvdUniverse& avd_universe,
                                                       VmOperations* vm_operations,
                                                       EventLoop* /*event_loop*/) {
    std::vector<std::shared_ptr<Service>> services;

    services.emplace_back(std::make_shared<CapabilitiesServiceImpl>(avd_universe));
    services.emplace_back(
            std::make_shared<SensorsServiceImpl>(avd_universe.GetSensorsPhysicalModel()));
    services.emplace_back(std::make_shared<SystemServiceImpl>(
            avd_universe.GetGuestStatus(), &avd_universe.GetGrpcNotificationChannel(),
            vm_operations, &avd_universe.GetClipboardChannel(),
            &avd_universe.GetFingerprintSensor()));

    if (vm_operations != nullptr) {
        auto operations_service = std::make_shared<OperationsServiceImpl>();
        services.emplace_back(operations_service);
        services.emplace_back(
                std::make_shared<HypervisorServiceImpl>(*vm_operations, operations_service));
    }

    return services;
}

}  // namespace goldfish::grpc::v2

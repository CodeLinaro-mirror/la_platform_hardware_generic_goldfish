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

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

#include <grpcpp/grpcpp.h>

#include "discovery/capabilities_service.grpc.pb.h"
#include "goldfish/avd_info/avd_info.h"

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

namespace goldfish::grpc::v2 {

/**
 * @brief Implements the AEMU v2 CapabilitiesService.
 *
 * Exposes a static, immutable snapshot of device limits, hardware configuration,
 * and peripheral access modes at connection time.
 */
class CapabilitiesServiceImpl final
        : public ::android::emulation::v2::discovery::CapabilitiesService::Service {
  public:
    explicit CapabilitiesServiceImpl(const ::goldfish::avd_info::AvdUniverse& avd_universe);

    ::grpc::Status GetDeviceCapabilities(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::discovery::GetDeviceCapabilitiesRequest* request,
            ::android::emulation::v2::discovery::DeviceCapabilities* reply) override;

  private:
    const ::goldfish::avd_info::AvdUniverse& avd_universe_;
};

}  // namespace goldfish::grpc::v2

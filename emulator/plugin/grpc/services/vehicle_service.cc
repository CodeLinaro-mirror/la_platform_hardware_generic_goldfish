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

#include "android/emulation/control/incubating/vehicle_service.h"

#include "absl/log/log.h"

namespace android {
namespace emulation {
namespace control {

VehicleServiceImpl::VehicleServiceImpl(VehicleChannel& channel) : mVehicleChannel(channel) {}

::grpc::ServerWriteReactor<VehicleEvent>* VehicleServiceImpl::streamVehicleEvents(
        ::grpc::CallbackServerContext* /*context*/, const ::google::protobuf::Empty* /*request*/) {
    return new VehicleEventStreamWriter(&mVehicleChannel.guest_to_host);
}

Status VehicleServiceImpl::setVehicleProperty(::grpc::ServerContext* /*context*/,
                                              const VehiclePropValue* request,
                                              ::google::protobuf::Empty* /*response*/) {
    // Convert to target channel type using serialization/deserialization helper
    std::string serialized;
    if (request->SerializeToString(&serialized)) {
        ::goldfish::avd_universe::vehicle::VehiclePropValue internal_val;
        if (internal_val.ParseFromString(serialized)) {
            mVehicleChannel.host_to_guest.SetValue(std::move(internal_val));
            return Status::OK;
        }
    }
    LOG(ERROR) << "Failed to map setVehicleProperty VHAL request to internal channel";
    return Status(::grpc::StatusCode::INTERNAL, "Failed to map VHAL value");
}

Status VehicleServiceImpl::getVehicleProperty(::grpc::ServerContext* /*context*/,
                                              const GetVehiclePropertyRequest* request,
                                              VehiclePropValue* response) {
    std::lock_guard<std::mutex> lock(mVehicleChannel.guest_state_mutex);
    auto it = mVehicleChannel.guest_state_map.find(
            {request->address().prop_id(), request->address().area_id()});
    if (it != mVehicleChannel.guest_state_map.end()) {
        // Map internal value to response
        std::string serialized;
        if (it->second.SerializeToString(&serialized)) {
            response->ParseFromString(serialized);
        } else {
            return Status(::grpc::StatusCode::INTERNAL, "Failed to serialize VHAL value");
        }
    } else {
        response->set_prop(request->address().prop_id());
        response->set_area_id(request->address().area_id());
        response->set_status(
                ::android::emulation::control::incubating::VehiclePropValue::NOT_AVAILABLE_GENERAL);
    }
    return Status::OK;
}

}  // namespace control
}  // namespace emulation
}  // namespace android

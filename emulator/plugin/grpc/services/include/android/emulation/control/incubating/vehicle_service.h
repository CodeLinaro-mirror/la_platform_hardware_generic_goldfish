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

#include "absl/log/log.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "goldfish/avd_universe/vehicle/vehicle_data.h"
#include "vehicle_service.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {

using ::android::emulation::control::incubating::GetVehiclePropertyRequest;
using ::android::emulation::control::incubating::VehicleEvent;
using ::android::emulation::control::incubating::VehiclePropValue;
using ::android::emulation::control::incubating::VehicleService;
using ::goldfish::avd_universe::vehicle::VehicleChannel;

using grpc::Status;

class VehicleEventStreamWriter
        : public BaseEventStreamWriter<VehicleEvent,
                                       ::goldfish::avd_universe::vehicle::VehiclePropValue> {
  public:
    explicit VehicleEventStreamWriter(
            android::base::eventing::CallbackEventSource<
                    ::goldfish::avd_universe::vehicle::VehiclePropValue>* listener)
            : BaseEventStreamWriter<VehicleEvent,
                                    ::goldfish::avd_universe::vehicle::VehiclePropValue>(listener) {
        Subscribe();
    }

    void EventArrived(const ::goldfish::avd_universe::vehicle::VehiclePropValue& event) override {
        VehicleEvent grpc_event;
        std::string serialized;
        if (event.SerializeToString(&serialized)) {
            if (grpc_event.mutable_property_change_event()->ParseFromString(serialized)) {
                Write(grpc_event);
            } else {
                LOG(ERROR) << "Failed to parse serialized VehiclePropValue for gRPC stream";
            }
        } else {
            LOG(ERROR) << "Failed to serialize VehiclePropValue for gRPC stream";
        }
    }
};

class VehicleServiceImpl final
        : public VehicleService::WithCallbackMethod_streamVehicleEvents<VehicleService::Service> {
  public:
    VehicleServiceImpl(VehicleChannel& channel);

    Status getVehicleProperty(::grpc::ServerContext* context,
                              const GetVehiclePropertyRequest* request,
                              VehiclePropValue* response) override;
    Status setVehicleProperty(::grpc::ServerContext* context, const VehiclePropValue* request,
                              ::google::protobuf::Empty* response) override;
    ::grpc::ServerWriteReactor<VehicleEvent>* streamVehicleEvents(
            ::grpc::CallbackServerContext* context,
            const ::google::protobuf::Empty* request) override;

  private:
    VehicleChannel& mVehicleChannel;
};

}  // namespace control
}  // namespace emulation
}  // namespace android

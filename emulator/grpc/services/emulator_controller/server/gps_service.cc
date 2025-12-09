// Copyright (C) 2025 The Android Open Source Project
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
#include "emulator/grpc/services/emulator_controller/server/gps_service.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_universe::gps::Location;
using grpc::ServerContext;
using grpc::Status;

namespace {
Location protoToLocation(const GpsState& proto) {
    return {
        .latitude = proto.latitude(),
        .longitude = proto.longitude(),
        .speed = proto.speed(),
        .bearing = proto.bearing(),
        .altitude = proto.altitude(),
        .satellites = proto.satellites(),
    };
}

GpsState locationToProto(const Location& location) {
    GpsState proto;
    proto.set_passiveupdate(false);  // Unused
    proto.set_latitude(location.latitude);
    proto.set_longitude(location.longitude);
    proto.set_speed(location.speed);
    proto.set_bearing(location.bearing);
    proto.set_altitude(location.altitude);
    proto.set_satellites(location.satellites);
    return proto;
}

}  // namespace

Status GpsServiceImpl::setGps(const GpsState& request) {
    mObservableLocation.setValue(protoToLocation(request));
    return Status::OK;
}

Status GpsServiceImpl::getGps(GpsState* reply) {
    *reply = locationToProto(mObservableLocation.getValue());
    return Status::OK;
}

}  // namespace control
}  // namespace emulation
}  // namespace android

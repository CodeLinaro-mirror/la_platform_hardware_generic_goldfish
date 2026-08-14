// Copyright 2026 The Android Open Source Project
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

#include "goldfish/netsim/netsim_connection.h"

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "android/status/status_macros.h"
#include "netsim_connection_internal.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "hw/core/qdev.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::netsim {

namespace {

const absl::Duration kConnectionDeadline = absl::Seconds(15);

struct NetsimConnectionData {
    std::string endpoint;
    std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient> grpc_client;
};

struct NetsimConnectionDev {
    DeviceClass parent_class;
    NetsimConnectionData* data;
};

absl::StatusOr<std::unique_ptr<android::emulation::control::BlockingEmulatorGrpcClient>> connect(
        const std::string& endpoint) {
    VLOG(1) << "netsim-connection: creating channel to netsimd endpoint - " << endpoint;

    android::emulation::control::Endpoint endpoint_config;
    endpoint_config.set_target(endpoint);

    ASSIGN_OR_RETURN(auto grpc_client,
                     android::emulation::control::EmulatorGrpcClientBuilder()
                             .WithEndpoint(endpoint_config)
                             // TODO(whollins): re-add interceptiors e.g.
                             //.WithInterceptor(std::make_unique<MetricsInterceptorFactory>());
                             .BuildBlocking());

    RETURN_IF_ERROR(grpc_client->Connect(kConnectionDeadline));

    VLOG(1) << "netsim-connection: connected";
    return grpc_client;
}

#define TYPE_NETSIM_CONNECTION "netsim-connection"
#define NETSIM_CONNECTION_DEV(obj) OBJECT_CHECK(NetsimConnectionDev, (obj), TYPE_NETSIM_CONNECTION)
#define NETSIM_CONNECTION_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(NetsimConnectionDev, obj, TYPE_NETSIM_CONNECTION)

void netsim_connection_realize(DeviceState* dev, Error** errp) {
    VLOG(1) << "netsim_connection_realize: " << object_get_canonical_path(OBJECT(dev));

    NetsimConnectionDev* nc = NETSIM_CONNECTION_DEV(dev);
    if (nc->data->endpoint.empty()) {
        error_setg(errp, "grpc_endpoint not set");
        return;
    }
    if (auto client = connect(nc->data->endpoint); !client.ok()) {
        error_setg(errp, "failed to establish netsim connection: %s - %s", dev->id,
                   client.status().ToString().c_str());
    } else {
        nc->data->grpc_client = *std::move(client);
    }
}

void netsim_connection_unrealize(DeviceState* dev) {
    VLOG(1) << "netsim_connection_unrealize";
    auto* nc = NETSIM_CONNECTION_DEV(dev);
    if (nc->data->grpc_client) {
        nc->data->grpc_client->Disconnect();
        nc->data->grpc_client.reset();
    }
}

void netsim_connection_set_grpc_endpoint(Object* obj, Visitor* v, const char* name, void* opaque,
                                         Error** errp) {
    auto* nc = NETSIM_CONNECTION_DEV(obj);
    char* endpoint;
    if (!visit_type_str(v, name, &endpoint, errp)) {
        return;
    }
    VLOG(1) << "netsim-connection: grpc_endpoint = " << endpoint;
    nc->data->endpoint = endpoint;
}

void netsim_connection_class_init(ObjectClass* oc, const void* data) {
    object_class_property_add(oc, "grpc_endpoint", "str", nullptr,
                              netsim_connection_set_grpc_endpoint, nullptr, nullptr);

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = netsim_connection_realize;
    dc->unrealize = netsim_connection_unrealize;
}

void netsim_connection_instance_init(Object* obj) {
    NETSIM_CONNECTION_DEV(obj)->data = new NetsimConnectionData;
    add_deletable_object(obj);
}

void netsim_connection_instance_finalize(Object* obj) {
    auto* nc = NETSIM_CONNECTION_DEV(obj);
    delete nc->data;
}

const TypeInfo netsim_connection_type_info = {
    .name = TYPE_NETSIM_CONNECTION,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NetsimConnectionDev),
    .instance_init = netsim_connection_instance_init,
    .instance_finalize = netsim_connection_instance_finalize,
    .class_init = netsim_connection_class_init,
};

}  // namespace

void netsim_connection_register_types(void) {
    type_register_static(&goldfish::netsim::netsim_connection_type_info);
}

absl::StatusOr<std::shared_ptr<android::emulation::control::EmulatorGrpcClientBase>>
get_connected_netsim_grpc_client() {
    Object* obj = object_resolve_type_unambiguous(TYPE_NETSIM_CONNECTION, nullptr);
    if (obj == nullptr) {
        return absl::NotFoundError("no netsim-connection found");
    }
    auto* nc = NETSIM_CONNECTION_DEV(obj);
    if (!nc->data->grpc_client) {
        return absl::NotFoundError("netsim-connection does not have a grpc client");
    }
    if (nc->data->grpc_client->GetConnectionState() !=
        android::emulation::control::ConnectionState::kConnected) {
        return absl::NotFoundError("netsim-connection grpc client is not connected");
    }
    return nc->data->grpc_client;
}

}  // namespace goldfish::netsim

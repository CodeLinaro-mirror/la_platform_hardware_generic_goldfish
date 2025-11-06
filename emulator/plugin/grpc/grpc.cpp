// Copyright 2024 The Android Open Source Project
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

#include "goldfish/grpc/grpc.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/escaping.h"

#include "aemu/base/process/Process.h"
#include "android/base/system/System.h"

#include "android/emulation/control/EmulatorService.h"
#include "android/emulation/control/GrpcServices.h"
#include "android/goldfish/config/emulator_advertisment.h"
#include "android/goldfish/display/MultiDisplay.h"
#include "android/goldfish/vm/VmInterface.h"
#include "android/utils/path.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/avd/avd-info.h"
#include "goldfish/avd/global-event-loop.h"
#include "goldfish/tools/aemu_version.h"

#include "grpc_display.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
}
// IWYU pragma: end_keep
// clang-format on

namespace fs = std::filesystem;
using ::android::base::System;
using ::android::emulation::control::EmulatorControllerService;
using ::android::goldfish::EmulatorAdvertisement;
using ::android::goldfish::EmulatorProperties;
using ::android::goldfish::IMultiDisplay;
using ::android::goldfish::VmOperations;
using ::goldfish::async::EventLoop;
using ::goldfish::async::QemuEventLoop;

namespace goldfish::grpc {

namespace {

struct GrpcConfig {
    std::string addr;
    fs::path tls_cer;
    fs::path tls_key;
    fs::path tls_ca;
    fs::path allowlist;
    fs::path discovery_path;
    bool use_token{false};
    int idle_timeout{0};
    int port{0};

    std::unique_ptr<EventLoop> qemu_loop;
    std::unique_ptr<EmulatorControllerService> grpc_service;
    std::unique_ptr<EmulatorAdvertisement> advertiser;
};

struct GrpcDev {
    DeviceClass parent_class;

    GrpcConfig *config;
};

#define TYPE_GRPC "grpc"
#define GRPC_DEV(obj) OBJECT_CHECK(GrpcDev, (obj), TYPE_GRPC)
#define GRPC_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(GrpcDev, obj, TYPE_GRPC)

// Generates a secure base64 encoded token of
// |cnt| bytes.
std::string generateToken(int cnt) {
    absl::BitGen gen;
    std::string buf(cnt, 0);  // Initialize a string of cnt bytes with 0s
    for (int i = 0; i < cnt; ++i) {
        auto byte = absl::Uniform<uint8_t>(gen);
        buf[i] = static_cast<char>(byte);
    }

    std::string encoded;
    absl::WebSafeBase64Escape(buf, &encoded);
    return encoded;
}

void grpc_realize(DeviceState* dev, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(dev);
    auto *config = grpc_device->config;

    if (!config->port) {
        error_setg(errp, "port attribute not set");
        return;
    }

    if (config->discovery_path.empty()) {
        error_setg(errp, "discovery_dir attribute not set");
        return;
    }

    if (!fs::exists(config->discovery_path)) {
        LOG(WARNING) << "Discovery directory: " << config->discovery_path.string() << ", does not exist. creating";
        path_mkdir_if_needed(config->discovery_path.string().c_str(), 0700);
    }

    auto& avdprops = goldfish::avd_info::getAvd().props();

    EmulatorProperties props{{"port.serial", std::to_string(avdprops.serial_number)},
                             {"emulator.build", BUILD_ID},
                             {"emulator.version", VERSION},
                             {"port.adb", std::to_string(avdprops.adb_port)},
                             {"avd.name", avdprops.avd_name},
                             {"avd.id", avdprops.avd_id},
                             {"avd.dir", avdprops.avd_content_path.string()},
                             // TODO(jansene):
                             {"cmdline",
                              "\"qemu-system-x86_64\" \"@testing\" \"-qt-hide-window\" "
                              "\"-grpc-use-token\""}};

    config->qemu_loop = QemuEventLoop::create();

    auto *registry = &goldfish::avd_info::connector_registry();

    auto service = ::android::emulation::control::getEmulatorController(
            VmOperations::qemuVmOperations(), registry, avdprops.avd_api, avdprops.hw_config, IMultiDisplay::instance(),
            config->qemu_loop.get());

    // TODO config->addr is set but not used anywhere
    auto builder = EmulatorControllerService::Builder()
                           .withLogging(true)
                           .withCertAndKey(config->tls_cer, config->tls_key, config->tls_ca)
                           .withAllowList(config->allowlist)
                           .withPortRange(config->port, config->port + 1)
                           .withService(service);

    if (config->idle_timeout > 0) {
        LOG(INFO) << "Terminating emulator if no activity after " << config->idle_timeout
                  << " seconds.";
        auto eventLoop = goldfish::async::globalEventLoop();
        builder.withIdleTimeout(std::chrono::seconds(config->idle_timeout), eventLoop);
    }

    if (config->use_token) {
        const int of64Bytes = 64;
        auto token = generateToken(of64Bytes);
        builder.withAuthToken(token);
        props["grpc.token"] = token;
    }
    fs::path jwkDir = config->discovery_path / std::to_string(::android::base::Process::me()->pid()) / "jwks" / generateToken(16);

    std::error_code ec;
    if (!System::get()->pathExists(jwkDir) && !fs::create_directories(jwkDir, ec)) {
        LOG(ERROR) << "Failed to create jwk directory " << jwkDir << " error: " << ec.message();
        error_setg(errp, "failed to create jwk directory");
        return;
    }

    fs::path jwkLoadedFile = jwkDir / "active.jwk";
    props["grpc.jwks"] = jwkDir.string();
    props["grpc.jwk_active"] = jwkLoadedFile.string();
    builder.withJwtAuthDiscoveryDir(jwkDir, jwkLoadedFile);

    config->grpc_service = builder.build();
    if (!config->grpc_service) {
        error_setg(errp, "failed to initialise grpc service");
        return;
    }

    props["grpc.port"] = std::to_string(config->grpc_service->port());
    props["grpc.allowlist"] = builder.allowlist().string();
    if (!config->tls_cer.empty()) {
        props["grpc.server_cert"] = config->tls_cer.string();
    }
    if (!config->tls_ca.empty()) {
        props["grpc.ca_root"] = config->tls_ca.string();
    }

    config->advertiser = std::make_unique<EmulatorAdvertisement>(props, config->discovery_path);
    config->advertiser->garbageCollect();
    config->advertiser->write();
}

void grpc_unrealize(DeviceState* dev) {
    VLOG(1) << "Finalizing gRPC endpoint";
    GrpcDev* grpc_device = GRPC_DEV(dev);
    auto *config = grpc_device->config;
    config->advertiser->remove();

    if (config->grpc_service) {
        // Explicitly cleanup resources. We do not want to do this at
        // program exit as we may be holding on to loopers, which threads
        // have likely been destroyed at that point.
        config->grpc_service->stop();
    }
}

void grpc_set_tls_cer(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->tls_cer = value;
}

void grpc_set_tls_key(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->tls_key = value;
}

void grpc_set_tls_ca(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->tls_ca = value;
}

void grpc_set_allowlist(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->allowlist = value;
}

void grpc_set_addr(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->addr = value;
}

void grpc_set_port(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    // Check for invalid input or overflow
    if (value < 0 || value > 65535) {
        error_setg(errp, "Port number should be between 0 and 65535, not: %d", value);
        return;
    }

    grpc_device->config->port = value;
}

void grpc_set_idle_timeout(Object* obj, Visitor* v, const char* name, void* opaque,
                                  Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    grpc_device->config->idle_timeout = value;
}

void grpc_set_enable_token(Object* obj, bool v, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->use_token = v;
}

void grpc_set_discovery_dir(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config->discovery_path = value;
}

static void grpc_instance_init(Object* obj) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config = new GrpcConfig{};
}

static void grpc_instance_finalize(Object* obj) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    delete grpc_device->config;
}

void grpc_class_init(ObjectClass* oc, void* data) {
    object_class_property_add_str(oc, "tls_cer", NULL, grpc_set_tls_cer);
    object_class_property_set_description(oc, "tls_cer",
                                          "PEM file with a X.509 public key certificate.");

    object_class_property_add_str(oc, "tls_key", NULL, grpc_set_tls_key);
    object_class_property_set_description(oc, "tls_key",
                                          "PEM file containing a private key used for TLS.");

    object_class_property_add_str(oc, "tls_ca", NULL, grpc_set_tls_ca);
    object_class_property_set_description(
            oc, "tls_ca",
            "PEM file containing a series of certificate authorities for client "
            "validation.");

    object_class_property_add_str(oc, "allowlist", NULL, grpc_set_allowlist);
    object_class_property_set_description(oc, "allowlist",
                                          "A json file describing the access rules.");

    object_class_property_add_str(oc, "addr", NULL, grpc_set_addr);
    object_class_property_set_description(oc, "addr",
                                          "Address to which the grpc service should be bound.");

    object_class_property_add(oc, "port", "int", NULL, grpc_set_port, NULL, NULL);
    object_class_property_set_description(oc, "port",
                                          "The port to which the grpc service should be bound.");

    object_class_property_add(oc, "idle_timeout", "int", NULL, grpc_set_idle_timeout, NULL, NULL);
    object_class_property_set_description(
            oc, "idle_timeout",
            "Shutdown the emulator after idle_timeout seconds of inactivity from the "
            "gRPC endpoint.");

    object_class_property_add_bool(oc, "token", NULL, grpc_set_enable_token);
    object_class_property_set_description(oc, "token",
                                          "Require an authorization header with "
                                          "a valid token for every grpc call.");

    object_class_property_add_str(oc, "discovery_dir", NULL, grpc_set_discovery_dir);

    // TODO should this be done on instance realization rather than setup?
    grpc_display_register();

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = grpc_realize;
    dc->unrealize = grpc_unrealize;
}

const TypeInfo grpc_type_info = {
    .name = TYPE_GRPC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(GrpcDev),
    .instance_init = grpc_instance_init,
    .instance_finalize = grpc_instance_finalize,
    .class_init = grpc_class_init,
};

} // namespace

void grpc_register_types(void) {
    type_register_static(&grpc_type_info);
}

} // namespace goldfish::grpc

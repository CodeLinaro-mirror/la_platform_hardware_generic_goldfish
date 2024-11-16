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

#include <pixman.h>
extern "C" {
#include "android/goldfish/grpc-service-device.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/surface.h"
// IWYU pragma: end_keep
// clang-format on
}

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <system_error>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"

#include "aemu/base/process/Process.h"
#include "android/base/system/System.h"
#include "android/emulation/control/EmulatorService.h"
#include "android/emulation/control/GrpcServices.h"
#include "android/goldfish/EmulatorAdvertisement.h"
#include "android/goldfish/avd-info.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/display/QemuDisplayTransformer.h"

namespace fs = std::filesystem;
using android::base::System;
using android::emulation::control::EmulatorControllerService;
using android::goldfish::Avd;
using android::goldfish::EmulatorAdvertisement;
using android::goldfish::EmulatorProperties;
using android::goldfish::QemuDisplayTransformer;

extern "C" const QAndroidVmOperations* const gQAndroidVmOperations;

// Generates a secure base64 encoded token of
// |cnt| bytes.
static std::string generateToken(int cnt) {
    absl::BitGen gen;
    std::string buf(cnt, 0);  // Initialize a string of cnt bytes with 0s
    for (int i = 0; i < cnt; ++i) {
        auto byte = absl::Uniform<uint8_t>(gen);
        buf[i] = static_cast<char>(byte);
    }

    std::string encoded;
    absl::Base64Escape(buf, &encoded);
    return encoded;
}

static std::unique_ptr<EmulatorAdvertisement> advertiser;
static std::unique_ptr<EmulatorControllerService> grpcService;
static QemuDisplayTransformer gDisplayTransformer{};
static pixman_image_t* g_image;

bool initialize(GrpcDeviceConfiguration* device) {
    auto avd = android::goldfish::avd_info::get_avd();
    auto registry = &android::goldfish::avd_info::deviceRegistry();

    // TODO(jansene): Update with actual data.
    EmulatorProperties props{
            {"port.serial", "5554"},
            {"emulator.build", "standalone-0"},
            {"emulator.version", "50.0.0"},
            {"port.adb", "5555"},
            {"avd.name", avd->name()},
            {"avd.id", avd->get("avd.ini.displayname", avd->name())},
            {"avd.dir", System ::pathAsString(avd->getContentPath())},
            {"cmdline", "\"qemu-system-x86_64\" \"@testing\" \"-qt-hide-window\""}};
    auto emulator = android::emulation::control::getEmulatorController(
            gQAndroidVmOperations, registry, avd, &gDisplayTransformer);
    auto builder = EmulatorControllerService::Builder()
                           .withLogging(true)
                           .withCertAndKey(device->tls_cer, device->tls_key, device->tls_ca)
                           .withVerboseLogging(true)
                           .withAllowList(device->allowlist)
                           .withPortRange(device->port, device->port + 1)
                           .withIdleTimeout(std::chrono::seconds(device->idle_timeout))
                           .withService(emulator);

    if (device->use_token) {
        const int of64Bytes = 64;
        auto token = generateToken(of64Bytes);
        builder.withAuthToken(token);
        props["grpc.token"] = token;
    }
    auto jwkDir = android::goldfish ::ConfigDirs::getDiscoveryDirectory() /
                  std::to_string(android::base::Process::me()->pid()) / "jwks" / generateToken(16);

    std::error_code ec;
    if (!System::get()->pathExists(jwkDir) && !fs::create_directories(jwkDir, ec)) {
        LOG(ERROR) << "Failed to create jwk directory " << jwkDir << " error: " << ec.message();
    }

    auto jwkLoadedFile = jwkDir / "active.jwk";
    props["grpc.jwks"] = jwkDir;
    props["grpc.jwk_active"] = jwkLoadedFile;
    builder.withJwtAuthDiscoveryDir(jwkDir, jwkLoadedFile);

    int port = -1;
    grpcService = builder.build();

    if (grpcService) {
        port = grpcService->port();
        props["grpc.port"] = std::to_string(port);
        props["grpc.allowlist"] = builder.allowlist();
        if (device->tls_cer) {
            props["grpc.server_cert"] = device->tls_cer;
        }
        if (device->tls_ca) {
            props["grpc.ca_root"] = device->tls_ca;
        }
    }

    advertiser = std::make_unique<EmulatorAdvertisement>(std::move(props));
    advertiser->garbageCollect();
    advertiser->write();

    return true;
}

void finalize(GrpcDeviceConfiguration* device) {
    VLOG(1) << "Finalizing gRPC endpoint";
    if (grpcService) {
        // Explicitly cleanup resources. We do not want to do this at
        // program exit as we may be holding on to loopers, which threads
        // have likely been destroyed at that point.
        grpcService->stop();
        grpcService = nullptr;
    }

    if (advertiser) {
        advertiser->remove();
    }

    if (g_image) {
        pixman_image_unref(g_image);
        g_image = nullptr;
    }
}

// TODO(jansene): Hook up the actual display rendering.
void grpc_dpy_gfx_update(struct DisplayChangeListener* dcl, int x, int y, int w, int h) {
    VLOG(1) << "grpc_dpy_gfx_update x: " << x << " y: " << y << " w: " << w << " h: " << h
            << " g_image: " << g_image;
    gDisplayTransformer.fireEvent(g_image);
}

void grpc_dpy_gfx_refresh(struct DisplayChangeListener* dcl) {
    VLOG(1) << "grpc_dpy_gfx_refresh";
}
void grpc_dpy_gfx_switch(struct DisplayChangeListener* dcl, struct DisplaySurface* new_surface) {
    if (g_image) {
        pixman_image_unref(g_image);
    }
    g_image = new_surface->image;
    VLOG(1) << "grpc_dpy_gfx_switch: " << new_surface->image;
    pixman_image_ref(g_image);
}
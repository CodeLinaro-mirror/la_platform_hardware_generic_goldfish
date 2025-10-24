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
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/emulator_advertisment.h"
#include "android/goldfish/display/MultiDisplay.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/avd/avd-info.h"
#include "goldfish/avd/global-event-loop.h"
#include "goldfish/grpc/grpc-service-device.h"
#include "goldfish/tools/aemu_version.h"

namespace fs = std::filesystem;
using android::base::System;
using android::emulation::control::EmulatorControllerService;
using android::goldfish::EmulatorAdvertisement;
using android::goldfish::EmulatorProperties;
using android::goldfish::IMultiDisplay;
using android::goldfish::VmOperations;
using goldfish::async::EventLoop;
using goldfish::async::QemuEventLoop;

struct GrpcDeviceConfigurationCpp {
    GrpcDeviceConfigurationCpp(std::unique_ptr<EmulatorControllerService> grpc,
                               EmulatorProperties props, std::unique_ptr<EventLoop> qemuLoop)
            : grpcService(std::move(grpc))
            , advertiser(std::move(props))
            , mQemuLoop(std::move(qemuLoop)) {
        advertiser.garbageCollect();
        advertiser.write();
    }

    ~GrpcDeviceConfigurationCpp() {
        advertiser.remove();

        if (grpcService) {
            // Explicitly cleanup resources. We do not want to do this at
            // program exit as we may be holding on to loopers, which threads
            // have likely been destroyed at that point.
            grpcService->stop();
        }
    }

    const std::unique_ptr<EmulatorControllerService> grpcService;
    EmulatorAdvertisement advertiser;
    std::unique_ptr<EventLoop> mQemuLoop;
};

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

bool initialize(GrpcDeviceConfiguration* device) {
    auto *avdprops = goldfish::avd_info::get_avd();
    if (!avdprops) {
        //return absl::NotFoundError("serious error - no avd properties available");
        LOG(ERROR) << "no avd properties available";
        return false;
    }

    auto *registry = &goldfish::avd_info::connector_registry();
    auto qemuLoop = QemuEventLoop::create();

    EmulatorProperties props{{"port.serial", std::to_string(avdprops->serial_number)},
                             {"emulator.build", BUILD_ID},
                             {"emulator.version", VERSION},
                             {"port.adb", std::to_string(avdprops->adb_port)},
                             {"avd.name", avdprops->avd_name},
                             {"avd.id", avdprops->avd_id},
                             {"avd.dir", avdprops->avd_content_path.string()},
                             // TODO(jansene):
                             {"cmdline",
                              "\"qemu-system-x86_64\" \"@testing\" \"-qt-hide-window\" "
                              "\"-grpc-use-token\""}};
    auto emulator = android::emulation::control::getEmulatorController(
            VmOperations::qemuVmOperations(), registry, avdprops->avd_api, avdprops->hw_config, IMultiDisplay::instance(),
            qemuLoop.get());
    auto builder = EmulatorControllerService::Builder()
                           .withLogging(true)
                           .withCertAndKey(device->tls_cer, device->tls_key, device->tls_ca)
                           .withAllowList(device->allowlist)
                           .withPortRange(device->port, device->port + 1)
                           .withService(emulator);

    if (device->idle_timeout > 0) {
        LOG(INFO) << "Terminating emulator if no activity after " << device->idle_timeout
                  << " seconds.";
        auto eventLoop = goldfish::async::globalEventLoop();
        builder.withIdleTimeout(std::chrono::seconds(device->idle_timeout), eventLoop);
    }

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
    props["grpc.jwks"] = jwkDir.string();
    props["grpc.jwk_active"] = jwkLoadedFile.string();
    builder.withJwtAuthDiscoveryDir(jwkDir.string(), jwkLoadedFile.string());

    std::unique_ptr<EmulatorControllerService> grpcService = builder.build();
    if (grpcService) {
        props["grpc.port"] = std::to_string(grpcService->port());
        props["grpc.allowlist"] = builder.allowlist();
        if (device->tls_cer) {
            props["grpc.server_cert"] = device->tls_cer;
        }
        if (device->tls_ca) {
            props["grpc.ca_root"] = device->tls_ca;
        }
    } else {
        // Should this be a startup error?
    }

    device->cppState = new GrpcDeviceConfigurationCpp(std::move(grpcService), std::move(props),
                                                      std::move(qemuLoop));
    return true;
}

void finalize(GrpcDeviceConfiguration* device) {
    VLOG(1) << "Finalizing gRPC endpoint";
    delete device->cppState;
    device->cppState = nullptr;
}

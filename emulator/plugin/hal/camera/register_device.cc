/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/devices/camera/register_device.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "android/base/system.h"

#include "goldfish/camera_image_providers/virtualscene/virtualscene.h"
#include "goldfish/devices/cable/error_plug.h"
#include "goldfish/devices/camera/camera_device.h"
#include "goldfish/devices/camera/camera_device_enumerator.h"
#include "goldfish/devices/camera/camera_image_provider_registry.h"
#include "goldfish/devices/camera/get_guest_emulated_camera_property.h"
#include "goldfish/parsing/from_chars.h"
#include "goldfish/parsing/get_key_value_str.h"
#include "goldfish/parsing/split2.h"
#include "ver/virtual_environment_renderer.h"

namespace goldfish::devices::camera {
using namespace std::literals;

using goldfish::devices::cable::ErrorPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using goldfish::parsing::GetKeyValueStr;
using goldfish::parsing::Split2;

namespace {
bool addImageProviderInfo(CameraImageProviderRegistry& dst, const std::string_view id,
                          const std::string_view params, const bool isBackFacing,
                          const sensors::PhysicalModel* physicalModel) {
    namespace cip = goldfish::camera_image_providers;

    if (id.empty() || (id == "none"sv) || (id == "emulated"sv)) {
        return true;
    }

    VerSceneConfig::Mode sceneMode = VerSceneConfig::Mode::Unknown;
    std::string_view sceneArgument = params;

    if (id.starts_with("webcam"sv)) {
        sceneMode = VerSceneConfig::Mode::Webcam;
        if (sceneArgument.empty()) {
            sceneArgument = id.substr(6);  // skip "webcam" in `id`
        }
    } else {
        sceneMode = VerSceneConfig::modeFromString(id);
    }

    if (sceneMode == VerSceneConfig::Mode::Unknown) {
        VLOG(1) << "Cam image id '" << id << "' is unknown.";
        return false;
    }

    const VerSceneConfig sceneConfig(sceneMode, sceneArgument);

    CameraImageProviderInfo info;
    if (!cip::virtualscene::getImageProviderInfo(&info, isBackFacing, physicalModel, sceneConfig)) {
        return false;
    }

    dst.add(info);
    return true;
}

PlugPtr createCameraDevice(SocketPtr socket, const std::string_view params,
                           const CameraImageProviderRegistry& registry,
                           const GrallocProvider& grallocProvider) {
    GrallocDetailsPtr grallocDetails = grallocProvider();
    if (!grallocDetails) {
        VLOG(1) << "Can't instantiate gralloc details.";
err:
        return std::make_shared<ErrorPlug>(std::move(socket));
    }

    constexpr std::string_view kParamName = "name"sv;

    const std::optional<std::string_view> maybeIndexStr = GetKeyValueStr(params, kParamName);
    if (!maybeIndexStr) {
        VLOG(1) << "Can't find the '" << kParamName << "' in '" << params << "'.";
        goto err;
    }

    const std::string_view indexStr = std::move(maybeIndexStr.value());
    const std::optional<size_t> maybeIndex = parsing::FromChars<size_t>(indexStr);
    if (!maybeIndex) {
        VLOG(1) << "Can't parse the camera index from '" << indexStr << "'.";
        goto err;
    }

    const size_t index = maybeIndex.value();
    const CameraImageProviderInfoCpp* infoCpp = registry[index];
    if (!infoCpp) {
        VLOG(1) << "Can't find the camera with index=" << index << " in the registry.";
        goto err;
    }

    const CameraImageProviderInfo& info = infoCpp->getInfo();
    const CameraImageProviderVtbl* vtbl;
    void* imageProvider = (info.vtbl->create)(&info, &vtbl);
    if (!imageProvider) {
        VLOG(1) << "Can't create an image provider for camera with index=" << index << ".";
        goto err;
    }

    return std::make_shared<CameraDevice>(std::move(socket), imageProvider, vtbl,
                                          std::move(grallocDetails));
}

}  // namespace

void RegisterDevice(const sensors::PhysicalModel* physicalModel, IConnectorRegistry* registry,
                    std::string* emulatedCameraProp, const android::goldfish::HardwareConfig& hw,
                    const std::filesystem::path& avdContentPath, GrallocProvider grallocProvider) {
    if (const std::string launcherDir =
                android::base::System::Get()->EnvGet("ANDROID_EMULATOR_LAUNCHER_DIR");
        !launcherDir.empty()) {
        const std::filesystem::path baseDir(launcherDir);

        // Local path resources will be searched in the given order.
        // TODO: use 'resources' folder similar to qemu-now?
        std::vector<std::filesystem::path> resourceBasePaths = {
            avdContentPath,
            baseDir / "data" / "virtualscene" / "default",
            baseDir / "data" / "virtualscene" / "Toren1BD",
            baseDir,
        };

        ver_initialize(resourceBasePaths, baseDir / "lib64" / "vulkan");
    }

    const auto [frontCameraId, frontCameraParams] = Split2(hw.hw_camera_front, ':');
    bool frontCameraEmulated = (frontCameraId == "emulated"sv);

    const auto [backCameraId, backCameraParams] = Split2(hw.hw_camera_back, ':');
    bool backCameraEmulated = (backCameraId == "emulated"sv);

    auto imageProvidersRegistry = std::make_shared<CameraImageProviderRegistry>();

    if (!addImageProviderInfo(*imageProvidersRegistry, frontCameraId, frontCameraParams, false,
                              physicalModel)) {
        frontCameraEmulated = true;
    }

    if (!addImageProviderInfo(*imageProvidersRegistry, backCameraId, backCameraParams, true,
                              physicalModel)) {
        backCameraEmulated = true;
    }

    *emulatedCameraProp = getGuestEmulatedCameraProperty(frontCameraEmulated, backCameraEmulated);

    registry->RegisterQemuDevice(std::string(CameraDeviceBase::kServiceName),
                                 [imageProvidersRegistry = std::move(imageProvidersRegistry),
                                  grallocProvider = std::move(grallocProvider)](
                                         SocketPtr socket, const std::shared_ptr<PingTopic>&,
                                         const std::string_view params) -> PlugPtr {
                                     if (params.empty()) {
                                         return std::make_shared<CameraDeviceEnumerator>(
                                                 std::move(socket), imageProvidersRegistry);
                                     } else {
                                         return createCameraDevice(std::move(socket), params,
                                                                   *imageProvidersRegistry,
                                                                   grallocProvider);
                                     }
                                 });
}

}  // namespace goldfish::devices::camera

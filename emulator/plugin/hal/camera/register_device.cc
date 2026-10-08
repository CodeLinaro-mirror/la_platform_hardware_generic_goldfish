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

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"

#include "goldfish/camera_image_providers/virtualscene/virtualscene.h"
#include "goldfish/devices/cable/error_plug.h"
#include "goldfish/devices/camera/camera_device.h"
#include "goldfish/devices/camera/camera_device_enumerator.h"
#include "goldfish/devices/camera/camera_image_provider_registry.h"
#include "goldfish/devices/camera/camera_image_source.h"
#include "goldfish/devices/camera/get_guest_emulated_camera_property.h"
#include "goldfish/parsing/from_chars.h"
#include "goldfish/parsing/get_key_value_str.h"
#include "goldfish/parsing/split2.h"

namespace goldfish::devices::camera {
using namespace std::literals;

using goldfish::devices::cable::ErrorPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using goldfish::parsing::GetKeyValueStr;
using goldfish::parsing::Split2;

namespace {
bool addImageProviderInfo(CameraImageProviderRegistry& dst, const CameraImageSource source,
                          const std::string_view id, const std::string_view params,
                          const bool isBackFacing, const sensors::PhysicalModel* physicalModel) {
    namespace cip = goldfish::camera_image_providers;

    VerSceneConfig sceneConfig;

    switch (source) {
    case CameraImageSource::NONE:
    case CameraImageSource::EMULATED:
        return true;

    case CameraImageSource::WEBCAM:
        //                                    skip "webcam" in `id`
        sceneConfig = VerSceneConfig(VerSceneConfig::Mode::Webcam,
                                     std::string(params.empty() ? id.substr(6) : params));
        break;

    case CameraImageSource::VIRTUALSCENE:
        sceneConfig = VerSceneConfig(VerSceneConfig::Mode::Mesh3D, std::string(params));
        break;

    case CameraImageSource::VIDEOFILE:
        sceneConfig = VerSceneConfig(VerSceneConfig::Mode::VideoFile, std::string(params));
        break;

    case CameraImageSource::IMAGEFILE:
        sceneConfig = VerSceneConfig(VerSceneConfig::Mode::ImageFile, std::string(params));
        break;

    default:
        return false;
    }

    CameraImageProviderInfo info;
    if (!cip::virtualscene::getImageProviderInfo(&info, isBackFacing, physicalModel,
                                                 std::move(sceneConfig))) {
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
                    GrallocProvider grallocProvider) {
    const auto [frontCameraId, frontCameraParams] = Split2(hw.hw_camera_front, ':');
    CameraImageSource frontCameraSource = getCameraImageSourceFromName(frontCameraId);

    const auto [backCameraId, backCameraParams] = Split2(hw.hw_camera_back, ':');
    CameraImageSource backCameraSource = getCameraImageSourceFromName(backCameraId);

    auto imageProvidersRegistry = std::make_shared<CameraImageProviderRegistry>();

    if (!addImageProviderInfo(*imageProvidersRegistry, frontCameraSource, frontCameraId,
                              frontCameraParams, false, physicalModel)) {
        frontCameraSource = CameraImageSource::EMULATED;
    }

    if (!addImageProviderInfo(*imageProvidersRegistry, backCameraSource, backCameraId,
                              backCameraParams, true, physicalModel)) {
        backCameraSource = CameraImageSource::EMULATED;
    }

    *emulatedCameraProp = getGuestEmulatedCameraProperty(frontCameraSource, backCameraSource);

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

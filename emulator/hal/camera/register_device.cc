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
#include <string_view>

#include "absl/log/log.h"

#include "android/camera/image_providers/imagefile.h"
#include "android/camera/image_providers/videofile.h"
#include "android/camera/image_providers/webcam.h"
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
                          const bool isBackFacing, CameraImageProviderRegistry& webcamRegistry) {
    namespace cip = goldfish::camera_image_providers;

    CameraImageProviderInfo info;

    switch (source) {
    case CameraImageSource::NONE:
    case CameraImageSource::EMULATED:
        return true;

    case CameraImageSource::WEBCAM:
        //                                                                 skip "webcam" in `id`
        if (const std::optional<size_t> maybeIndex = parsing::FromChars<size_t>(id.substr(6))) {
            if (CameraImageProviderInfoCpp* const info = webcamRegistry[maybeIndex.value()]) {
                info->setBackFacing(isBackFacing);
                dst.add(std::move(*info));
                return true;
            }
        }
        return false;

    case CameraImageSource::VIRTUALSCENE:
        if (!cip::virtualscene::getImageProviderInfo(&info, isBackFacing)) {
            return false;
        }
        break;

    case CameraImageSource::VIDEOFILE:
        if (getVideofileImageProviderInfo(&info, isBackFacing ? 1 : 0,
                                          std::string(params).c_str())) {
            return false;
        }
        break;

    case CameraImageSource::IMAGEFILE:
        if (getImagefileImageProviderInfo(&info, isBackFacing ? 1 : 0,
                                          std::string(params).c_str())) {
            return false;
        }
        break;

    default:
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

void RegisterDevice(IConnectorRegistry* registry, std::string* emulatedCameraProp,
                    const android::goldfish::HardwareConfig& hw, GrallocProvider grallocProvider) {
    const auto [frontCameraId, frontCameraParams] = Split2(hw.hw_camera_front, ':');
    CameraImageSource frontCameraSource = getCameraImageSourceFromName(frontCameraId);

    const auto [backCameraId, backCameraParams] = Split2(hw.hw_camera_back, ':');
    CameraImageSource backCameraSource = getCameraImageSourceFromName(backCameraId);

    *emulatedCameraProp = getGuestEmulatedCameraProperty(frontCameraSource, backCameraSource);

    CameraImageProviderRegistry webcamRegistry;
    if ((frontCameraSource == CameraImageSource::WEBCAM) ||
        (backCameraSource == CameraImageSource::WEBCAM)) {
        if (enumerateWebcamImageProviders(&CameraImageProviderRegistry::addStatic,
                                          &webcamRegistry)) {
            webcamRegistry.clear();  // something went wrong
        }
    }

    auto imageProvidersRegistry = std::make_shared<CameraImageProviderRegistry>();

    if (!addImageProviderInfo(*imageProvidersRegistry, frontCameraSource, frontCameraId,
                              frontCameraParams, false, webcamRegistry)) {
        frontCameraSource = CameraImageSource::EMULATED;
    }

    if (!addImageProviderInfo(*imageProvidersRegistry, backCameraSource, backCameraId,
                              backCameraParams, true, webcamRegistry)) {
        backCameraSource = CameraImageSource::EMULATED;
    }

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

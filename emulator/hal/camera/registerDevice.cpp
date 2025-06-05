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

#include "android/camera/registerDevice.h"

#include <memory>
#include <optional>
#include <string_view>

#include "absl/log/log.h"

#include "android/camera/CameraDevice.h"
#include "android/camera/CameraDeviceEnumerator.h"
#include "android/camera/CameraImageProviderRegistry.h"
#include "android/camera/image_providers/imagefile.h"
#include "android/camera/image_providers/videofile.h"
#include "android/camera/image_providers/virtualscene.h"
#include "android/camera/image_providers/webcam.h"
#include "goldfish/devices/cable/ErrorPlug.h"
#include "goldfish/parsing/fromChars.h"
#include "goldfish/parsing/getKeyValueStr.h"
#include "goldfish/parsing/split2.h"

namespace goldfish::devices::camera {
using namespace std::literals;

using goldfish::devices::cable::ErrorPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using goldfish::parsing::getKeyValueStr;
using goldfish::parsing::split2;

namespace {
enum class CameraImageSource {
    EMULATED,
    WEBCAM,
    VIRTUALSCENE,
    VIDEOFILE,
    IMAGEFILE,
};

CameraImageSource getCameraImageSourceFromName(const std::string_view name) {
    if (name.starts_with("webcam"sv)) {
        return CameraImageSource::WEBCAM;
    } else if (name == "virtualscene"sv) {
        return CameraImageSource::VIRTUALSCENE;
    } else if (name == "videofile"s) {
        return CameraImageSource::VIDEOFILE;
    } else if (name == "imagefile"s) {
        return CameraImageSource::IMAGEFILE;
    } else if ((name != "emulated"sv) && !name.empty()) {
        LOG(WARNING) << "camera: unexpected camera source: '" << name << "'";
    }

    return CameraImageSource::EMULATED;
}

bool addImageProviderInfo(CameraImageProviderRegistry& dst, const CameraImageSource source,
                          const std::string_view id, const std::string_view params,
                          const bool isBackFacing, CameraImageProviderRegistry& webcamRegistry) {
    CameraImageProviderInfo info;

    switch (source) {
    case CameraImageSource::EMULATED:
        return true;

    case CameraImageSource::WEBCAM:
        //                                                                 skip "webcam" in `id`
        if (const std::optional<size_t> maybeIndex = parsing::fromChars<size_t>(id.substr(6))) {
            if (CameraImageProviderInfoCpp* const info = webcamRegistry[maybeIndex.value()]) {
                info->setBackFacing(isBackFacing);
                dst.add(std::move(*info));
                return true;
            }
        }
        return false;

    case CameraImageSource::VIRTUALSCENE:
        if (getVirtualsceneImageProviderInfo(&info, isBackFacing ? 1 : 0)) {
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

std::string getGuestEmulatedCameraProperty(const CameraImageSource front,
                                           const CameraImageSource back) {
    if (front == CameraImageSource::EMULATED) {
        if (back == CameraImageSource::EMULATED) {
            return "both"s;
        } else {
            return "front"s;
        }
    } else if (back == CameraImageSource::EMULATED) {
        return "back"s;
    } else {
        return "none"s;
    }
}

PlugPtr createCameraDevice(SocketPtr socket, const std::string_view params,
                           const CameraImageProviderRegistry& registry,
                           const GrallocDetailsPtr& grallocDetails) {
    constexpr std::string_view kParamName = "name"sv;

    const std::optional<std::string_view> maybeIndexStr = getKeyValueStr(params, kParamName);
    if (!maybeIndexStr) {
        VLOG(1) << "Can't find the '" << kParamName << "' in '" << params << "'.";
err:
        return std::make_shared<ErrorPlug>(std::move(socket));
    }

    const std::string_view indexStr = std::move(maybeIndexStr.value());
    const std::optional<size_t> maybeIndex = parsing::fromChars<size_t>(indexStr);
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

    return std::make_shared<CameraDevice>(std::move(socket), imageProvider, vtbl, grallocDetails);
}

}  // namespace

void registerDevice(IConnectorRegistry* registry, std::string* emulatedCameraProp,
                    const android::goldfish::Avd& avd, GrallocDetailsPtr grallocDetails) {
    CameraImageSource frontCameraSource;
    CameraImageSource backCameraSource;

    if (grallocDetails) {
        const auto [frontCameraId, frontCameraParams] = split2(avd.hw().hw_camera_front, ':');
        frontCameraSource = getCameraImageSourceFromName(frontCameraId);

        const auto [backCameraId, backCameraParams] = split2(avd.hw().hw_camera_back, ':');
        backCameraSource = getCameraImageSourceFromName(backCameraId);

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

        registry->registerQemuDevice("camera"s,
                                     [imageProvidersRegistry = std::move(imageProvidersRegistry),
                                      grallocDetails = std::move(grallocDetails)](
                                             SocketPtr socket, const std::shared_ptr<PingTopic>&,
                                             const std::string_view params) -> PlugPtr {
                                         if (params.empty()) {
                                             return std::make_shared<CameraDeviceEnumerator>(
                                                     std::move(socket), imageProvidersRegistry);
                                         } else {
                                             return createCameraDevice(std::move(socket), params,
                                                                       *imageProvidersRegistry,
                                                                       grallocDetails);
                                         }
                                     });
    } else {
        frontCameraSource = CameraImageSource::EMULATED;
        backCameraSource = CameraImageSource::EMULATED;
    }

    *emulatedCameraProp = getGuestEmulatedCameraProperty(frontCameraSource, backCameraSource);
}

}  // namespace goldfish::devices::camera

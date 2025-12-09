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

#include "goldfish/gvk/util/select_physical_device.h"

#include <array>
#include <string_view>
#include <vector>

#include "absl/log/log.h"

namespace goldfish::gvk::util {
namespace {
using PhysicalDeviceAndPropPair = std::pair<VkPhysicalDevice, VkPhysicalDeviceProperties>;
using PhysicalDevicesAndProps = std::vector<PhysicalDeviceAndPropPair>;

PhysicalDevicesAndProps getAllPhysicalDevicesAndProps(const InstanceDispatch& instanceDispatch) {
    VkResult result;

    uint32_t devCount = 0;
    result = instanceDispatch.enumeratePhysicalDevices(&devCount, nullptr);
    if (result != VK_SUCCESS) {
        LOG(ERROR) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }

    std::vector<VkPhysicalDevice> devs(devCount);
    result = instanceDispatch.enumeratePhysicalDevices(&devCount, devs.data());
    if (result != VK_SUCCESS) {
        LOG(ERROR) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }

    PhysicalDevicesAndProps physicalDevicesAndProps(devCount);
    for (uint32_t i = 0; i < devCount; ++i) {
        const VkPhysicalDevice dev = devs[i];
        physicalDevicesAndProps[i] = {dev, instanceDispatch.getPhysicalDeviceProperties(dev)};
    }

    std::sort(physicalDevicesAndProps.begin(), physicalDevicesAndProps.end(),
              [](const PhysicalDeviceAndPropPair& lhsp, const PhysicalDeviceAndPropPair& rhsp) {
                  const VkPhysicalDeviceProperties& lhs = lhsp.second;
                  const VkPhysicalDeviceProperties& rhs = rhsp.second;

                  const int byName =
                          ::strncmp(lhs.deviceName, rhs.deviceName, sizeof(lhs.deviceName));
                  if (byName < 0) {
                      return true;
                  } else if (byName > 0) {
                      return false;
                  } else {
                      return ::memcmp(lhs.pipelineCacheUUID, rhs.pipelineCacheUUID,
                                      sizeof(lhs.pipelineCacheUUID)) < 0;
                  }
              });

    return physicalDevicesAndProps;
}

const char* getRejectionReasonStr(const PhysicalDeviceRejectionReason reason) {
    switch (reason) {
    case PhysicalDeviceRejectionReason::NONE:
        break;

    case PhysicalDeviceRejectionReason::NOT_VULKAN:
        return "NOT_VULKAN";

    case PhysicalDeviceRejectionReason::LOW_API_VERSION:
        return "LOW_API_VERSION";

    case PhysicalDeviceRejectionReason::MISSING_EXTENSIONS:
        return "MISSING_EXTENSIONS";

    case PhysicalDeviceRejectionReason::SKIPPED:
        return "SKIPPED";

    case PhysicalDeviceRejectionReason::BLOCKLISTED:
        return "BLOCKLISTED";
    }

    return "unknown";
}
}  // namespace

VkPhysicalDevice selectPhysicalDevice(const InstanceDispatch& instanceDispatch,
                                      const PhysicalDeviceScoringFunction& scoringFunc,
                                      const bool verbose) {
    const PhysicalDevicesAndProps physicalDevicesAndProps =
            getAllPhysicalDevicesAndProps(instanceDispatch);

    int32_t bestScoreSoFar = -1;
    VkPhysicalDevice bestDeviceSoFar = VK_NULL_HANDLE;

    const uint32_t devCount = physicalDevicesAndProps.size();
    for (uint32_t i = 0; i < devCount; ++i) {
        const PhysicalDeviceAndPropPair& dp = physicalDevicesAndProps[i];
        const VkPhysicalDeviceProperties& props = dp.second;

        int32_t score;
        if (VK_API_VERSION_VARIANT(props.apiVersion) > 0) {
            score = -static_cast<int32_t>(PhysicalDeviceRejectionReason::NOT_VULKAN);
        } else {
            score = scoringFunc(props);
            if (score > bestScoreSoFar) {
                bestScoreSoFar = score;
                bestDeviceSoFar = dp.first;
            }
        }

        if (verbose) {
            const char* scoreStr;
            char scoreStrBuf[16];
            if (score >= 0) {
                ::snprintf(scoreStrBuf, sizeof(scoreStrBuf), "%d", score);
                scoreStr = scoreStrBuf;
            } else {
                scoreStr =
                        getRejectionReasonStr(static_cast<PhysicalDeviceRejectionReason>(-score));
            }

            LOG(INFO) << i << ": "
                      << "name: '" << props.deviceName
                      << "', apiVersion: " << VK_API_VERSION_MAJOR(props.apiVersion) << '.'
                      << VK_API_VERSION_MINOR(props.apiVersion) << '.'
                      << VK_API_VERSION_PATCH(props.apiVersion) << std::hex
                      << ", vendorID: " << props.vendorID << ", deviceID: " << props.deviceID
                      << ", driverVersion: " << props.driverVersion << ", score: " << scoreStr;
        }
    }

    return bestDeviceSoFar;
}

VkPhysicalDevice selectPhysicalDeviceByIndex(const InstanceDispatch& instanceDispatch,
                                             const size_t index) {
    const PhysicalDevicesAndProps physicalDevicesAndProps =
            getAllPhysicalDevicesAndProps(instanceDispatch);

    if (index < physicalDevicesAndProps.size()) {
        return physicalDevicesAndProps[index].first;
    } else {
        return VK_NULL_HANDLE;
    }
}

}  // namespace goldfish::gvk::util

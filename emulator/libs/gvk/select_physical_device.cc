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

PhysicalDevicesAndProps GetAllPhysicalDevicesAndProps(const InstanceDispatch& instance_dispatch) {
    VkResult result;

    uint32_t dev_count = 0;
    result = instance_dispatch.enumeratePhysicalDevices(&dev_count, nullptr);
    if (result != VK_SUCCESS) {
        LOG(ERROR) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }

    std::vector<VkPhysicalDevice> devs(dev_count);
    result = instance_dispatch.enumeratePhysicalDevices(&dev_count, devs.data());
    if (result != VK_SUCCESS) {
        LOG(ERROR) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }

    PhysicalDevicesAndProps physical_devices_and_props(dev_count);
    for (uint32_t i = 0; i < dev_count; ++i) {
        const VkPhysicalDevice dev = devs[i];
        physical_devices_and_props[i] = {dev, instance_dispatch.getPhysicalDeviceProperties(dev)};
    }

    std::sort(physical_devices_and_props.begin(), physical_devices_and_props.end(),
              [](const PhysicalDeviceAndPropPair& lhsp, const PhysicalDeviceAndPropPair& rhsp) {
                  const VkPhysicalDeviceProperties& lhs = lhsp.second;
                  const VkPhysicalDeviceProperties& rhs = rhsp.second;

                  const int by_name =
                          ::strncmp(lhs.deviceName, rhs.deviceName, sizeof(lhs.deviceName));
                  if (by_name < 0) {
                      return true;
                  } else if (by_name > 0) {
                      return false;
                  } else {
                      return ::memcmp(lhs.pipelineCacheUUID, rhs.pipelineCacheUUID,
                                      sizeof(lhs.pipelineCacheUUID)) < 0;
                  }
              });

    return physical_devices_and_props;
}

const char* GetRejectionReasonString(const PhysicalDeviceRejectionReason reason) {
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

VkPhysicalDevice SelectPhysicalDevice(const InstanceDispatch& instance_dispatch,
                                      const PhysicalDeviceScoringFunction& scoring_func,
                                      const bool verbose) {
    const PhysicalDevicesAndProps physical_devices_and_props =
            GetAllPhysicalDevicesAndProps(instance_dispatch);

    int32_t best_score_so_far = -1;
    VkPhysicalDevice best_device_so_far = VK_NULL_HANDLE;

    const uint32_t dev_count = physical_devices_and_props.size();
    for (uint32_t i = 0; i < dev_count; ++i) {
        const PhysicalDeviceAndPropPair& dp = physical_devices_and_props[i];
        const VkPhysicalDeviceProperties& props = dp.second;

        int32_t score;
        if (VK_API_VERSION_VARIANT(props.apiVersion) > 0) {
            score = -static_cast<int32_t>(PhysicalDeviceRejectionReason::NOT_VULKAN);
        } else {
            score = scoring_func(props);
            if (score > best_score_so_far) {
                best_score_so_far = score;
                best_device_so_far = dp.first;
            }
        }

        if (verbose) {
            const char* score_str;
            char score_str_buf[16];
            if (score >= 0) {
                ::snprintf(score_str_buf, sizeof(score_str_buf), "%d", score);
                score_str = score_str_buf;
            } else {
                score_str = GetRejectionReasonString(
                        static_cast<PhysicalDeviceRejectionReason>(-score));
            }

            LOG(INFO) << i << ": "
                      << "name: '" << props.deviceName
                      << "', apiVersion: " << VK_API_VERSION_MAJOR(props.apiVersion) << '.'
                      << VK_API_VERSION_MINOR(props.apiVersion) << '.'
                      << VK_API_VERSION_PATCH(props.apiVersion) << std::hex
                      << ", vendorID: " << props.vendorID << ", deviceID: " << props.deviceID
                      << ", driverVersion: " << props.driverVersion << ", score: " << score_str;
        }
    }

    return best_device_so_far;
}

VkPhysicalDevice SelectPhysicalDeviceByIndex(const InstanceDispatch& instance_dispatch,
                                             const size_t index) {
    const PhysicalDevicesAndProps physical_devices_and_props =
            GetAllPhysicalDevicesAndProps(instance_dispatch);

    if (index < physical_devices_and_props.size()) {
        return physical_devices_and_props[index].first;
    } else {
        return VK_NULL_HANDLE;
    }
}

}  // namespace goldfish::gvk::util

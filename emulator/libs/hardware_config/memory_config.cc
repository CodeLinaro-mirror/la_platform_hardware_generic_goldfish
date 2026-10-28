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

#include "android/goldfish/memory_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "android/status/status_macros.h"
#include "host-common/hw-lcd.h"

namespace android::goldfish {

namespace {
hwLcd_screenSize_t HwLcdGetScreenSize(int height_px, int width_px, int density) {
    if (density <= 0) {
        return LCD_SIZE_NORMAL;
    }
    const double screen_inches = std::sqrt(std::pow(static_cast<double>(height_px) / density, 2) +
                                           std::pow(static_cast<double>(width_px) / density, 2));

    if (screen_inches >= 7.5) {
        return LCD_SIZE_XLARGE;
    }
    if (screen_inches >= 5) {
        return LCD_SIZE_LARGE;
    }
    if (screen_inches >= 3.6) {
        return LCD_SIZE_NORMAL;
    }
    return LCD_SIZE_SMALL;
}

hwLcd_screenSize_t AndroidHwConfigGetScreenSize(const HardwareConfig& config) {
    return HwLcdGetScreenSize(config.hw_lcd_height, config.hw_lcd_width, config.hw_lcd_density);
}

/**
 * @brief Minimum VM heap requirement (in MiB) for a display density tier.
 *
 * Each lookup table must be sorted in strictly descending `min_density` order
 * and end with a `{0, fallback_mb}` entry, as `LookupMinHeapByDensity` returns
 * the first entry satisfying `density >= min_density`.
 */
struct DensityHeapThreshold {
    int min_density;
    int min_heap_mb;
};

/**
 * @brief Per-screen-size CDD minimum VM heap tables for an Android API tier.
 *
 * `kApiHeapConfigs` must be sorted in strictly descending `min_api_level` order
 * so the first matching tier (`api_level >= min_api_level`) applies.
 */
struct ApiHeapConfig {
    int min_api_level;
    std::span<const DensityHeapThreshold> xlarge;
    std::span<const DensityHeapThreshold> large;
    std::span<const DensityHeapThreshold> normal;
};

constexpr bool IsSortedDescending(std::span<const DensityHeapThreshold> thresholds) {
    if (thresholds.empty()) {
        return false;
    }
    for (size_t i = 1; i < thresholds.size(); ++i) {
        if (thresholds[i - 1].min_density <= thresholds[i].min_density) {
            return false;
        }
    }
    return true;
}

constexpr bool AreApiHeapConfigsValid(std::span<const ApiHeapConfig> configs) {
    if (configs.empty()) {
        return false;
    }
    for (size_t i = 0; i < configs.size(); ++i) {
        if (i > 0 && configs[i - 1].min_api_level <= configs[i].min_api_level) {
            return false;
        }
        if (!IsSortedDescending(configs[i].xlarge) || !IsSortedDescending(configs[i].large) ||
            !IsSortedDescending(configs[i].normal)) {
            return false;
        }
    }
    return true;
}

int LookupMinHeapByDensity(std::span<const DensityHeapThreshold> thresholds, int density) {
    for (const DensityHeapThreshold& entry : thresholds) {
        if (density >= entry.min_density) {
            return entry.min_heap_mb;
        }
    }
    return thresholds.back().min_heap_mb;
}

/**
 * @brief CDD Section 3.7 minimum VM heap tables by API level and screen size
 * (sorted in strictly descending `min_density` order).
 */
constexpr DensityHeapThreshold kApi23XlargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 768},
    {LCD_DENSITY_560DPI, 576},
    {LCD_DENSITY_XXHDPI, 384},
    {LCD_DENSITY_420DPI, 336},
    {LCD_DENSITY_400DPI, 288},
    {LCD_DENSITY_360DPI, 240},
    {LCD_DENSITY_XHDPI, 192},
    {LCD_DENSITY_280DPI, 144},
    {LCD_DENSITY_TVDPI, 96},
    {LCD_DENSITY_MDPI, 80},
    {0, 48},
};
constexpr DensityHeapThreshold kApi23LargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 512},
    {LCD_DENSITY_560DPI, 384},
    {LCD_DENSITY_XXHDPI, 256},
    {LCD_DENSITY_420DPI, 228},
    {LCD_DENSITY_400DPI, 192},
    {LCD_DENSITY_360DPI, 160},
    {LCD_DENSITY_XHDPI, 128},
    {LCD_DENSITY_280DPI, 96},
    {LCD_DENSITY_TVDPI, 80},
    {LCD_DENSITY_MDPI, 48},
    {0, 32},
};
constexpr DensityHeapThreshold kApi23DefaultHeap[] = {
    {LCD_DENSITY_XXXHDPI, 256}, {LCD_DENSITY_560DPI, 192},
    {LCD_DENSITY_XXHDPI, 128},  {LCD_DENSITY_420DPI, 112},
    {LCD_DENSITY_400DPI, 96},   {LCD_DENSITY_XHDPI, 80},
    {LCD_DENSITY_TVDPI, 48},    {0, 32},
};

constexpr DensityHeapThreshold kApi22XlargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 768}, {LCD_DENSITY_560DPI, 576}, {LCD_DENSITY_XXHDPI, 384},
    {LCD_DENSITY_400DPI, 288},  {LCD_DENSITY_XHDPI, 192},  {LCD_DENSITY_280DPI, 144},
    {LCD_DENSITY_TVDPI, 96},    {LCD_DENSITY_MDPI, 80},    {0, 48},
};
constexpr DensityHeapThreshold kApi22LargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 512}, {LCD_DENSITY_560DPI, 384}, {LCD_DENSITY_XXHDPI, 256},
    {LCD_DENSITY_400DPI, 192},  {LCD_DENSITY_XHDPI, 128},  {LCD_DENSITY_280DPI, 96},
    {LCD_DENSITY_TVDPI, 80},    {LCD_DENSITY_MDPI, 48},    {0, 32},
};
constexpr DensityHeapThreshold kApi22DefaultHeap[] = {
    {LCD_DENSITY_XXXHDPI, 256},
    {LCD_DENSITY_560DPI, 192},
    {LCD_DENSITY_XXHDPI, 128},
    {LCD_DENSITY_400DPI, 96},
    {LCD_DENSITY_XHDPI, 80},
    {LCD_DENSITY_TVDPI, 48},
    {0, 32},
};

constexpr DensityHeapThreshold kApi21XlargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 768},
    {LCD_DENSITY_560DPI, 576},
    {LCD_DENSITY_XXHDPI, 384},
    {LCD_DENSITY_400DPI, 288},
    {LCD_DENSITY_XHDPI, 192},
    {LCD_DENSITY_TVDPI, 96},
    {0, 64},
};
constexpr DensityHeapThreshold kApi21LargeHeap[] = {
    {LCD_DENSITY_XXXHDPI, 512}, {LCD_DENSITY_560DPI, 384},
    {LCD_DENSITY_XXHDPI, 256},  {LCD_DENSITY_400DPI, 192},
    {LCD_DENSITY_XHDPI, 128},   {LCD_DENSITY_TVDPI, 64},
    {LCD_DENSITY_MDPI, 32},     {0, 16},
};
constexpr DensityHeapThreshold kApi21DefaultHeap[] = {
    {LCD_DENSITY_XXXHDPI, 256},
    {LCD_DENSITY_560DPI, 192},
    {LCD_DENSITY_XXHDPI, 128},
    {LCD_DENSITY_400DPI, 96},
    {LCD_DENSITY_XHDPI, 64},
    {LCD_DENSITY_TVDPI, 32},
    {0, 16},
};

constexpr ApiHeapConfig kApiHeapConfigs[] = {
    {23, kApi23XlargeHeap, kApi23LargeHeap, kApi23DefaultHeap},
    {22, kApi22XlargeHeap, kApi22LargeHeap, kApi22DefaultHeap},
    {21, kApi21XlargeHeap, kApi21LargeHeap, kApi21DefaultHeap},
};
static_assert(AreApiHeapConfigsValid(kApiHeapConfigs));

int AndroidHwConfigGetMinVmHeapSize(const HardwareConfig& config, int api_level) {
    // Taken from requirements in CDD Documents on VM/Runtime Compatibility.
    // API levels < 21 are rejected earlier by CalculateMinimumRam.
    // TODO: android wear minimums
    const ApiHeapConfig* selected = &kApiHeapConfigs[std::size(kApiHeapConfigs) - 1];
    for (const ApiHeapConfig& api_config : kApiHeapConfigs) {
        if (api_level >= api_config.min_api_level) {
            selected = &api_config;
            break;
        }
    }
    const hwLcd_screenSize_t screen_size = AndroidHwConfigGetScreenSize(config);
    if (screen_size >= LCD_SIZE_XLARGE) {
        return LookupMinHeapByDensity(selected->xlarge, config.hw_lcd_density);
    }
    if (screen_size >= LCD_SIZE_LARGE) {
        return LookupMinHeapByDensity(selected->large, config.hw_lcd_density);
    }
    return LookupMinHeapByDensity(selected->normal, config.hw_lcd_density);
}
}  // namespace

absl::StatusOr<int> MemoryConfig::CalculateMinimumRam(const HardwareConfig& hw, int api_level,
                                                      std::optional<DeviceType> device_type) {
    if (api_level < 21) {
        return absl::InvalidArgumentError(absl::StrCat(
                "We do not support api level < 21. Current API level is: ", api_level));
    }

    int min_ram = 1024;
    if (api_level >= 37) {
        min_ram = 4096;
    } else if (api_level >= 34) {
        min_ram = 2560;
    } else if (api_level >= 29) {
        min_ram = 2048;
    } else if (api_level >= 26) {
        min_ram = 1536;
    }

    const bool is_foldable = hw.hw_sensor_hinge || hw.hw_sensor_roll;
    const bool is_large_screen =
            (static_cast<long long>(hw.hw_lcd_width) * static_cast<long long>(hw.hw_lcd_height)) >=
            3LL * 1000LL * 1000LL;

    if (api_level >= 33 && (is_foldable || is_large_screen)) {
        min_ram = std::max(min_ram, 4096);
    }

    if (device_type.has_value()) {
        if (device_type.value() == DeviceType::kTv) {
            min_ram = std::max(min_ram, 1024);
        } else if (device_type.value() == DeviceType::kXr) {
            min_ram = std::max(min_ram, 4096);
        } else if (device_type.value() == DeviceType::kWear) {
            min_ram = std::min(min_ram, 2048);
        }
    }

    return min_ram;
}

absl::Status MemoryConfig::FinalizeRamAndHeapSize(HardwareConfig& hw, int api_level) {
    int ram_size = hw.hw_ramSize;
    if (ram_size <= 0) {
        LOG(WARNING) << "RAM size not specified in AVD, defaulting to 2GiB";
        ram_size = 2048;
    }
    auto memory_size_mi_b = static_cast<uint64_t>(ram_size);

    // enforce CDD minimums
    ASSIGN_OR_RETURN(int min_ram, CalculateMinimumRam(hw, api_level));

    if (std::cmp_less(memory_size_mi_b, min_ram)) {
        LOG(INFO) << "Enforcing minimum RAM requirement; "
                  << "[reason='api_or_device_constraint', "
                  << "original_mib=" << memory_size_mi_b << ", "
                  << "target_mib=" << min_ram << ", "
                  << "api_level=" << api_level << "]";
        memory_size_mi_b = min_ram;
    }

    hw.hw_ramSize = static_cast<int>(memory_size_mi_b);
    VLOG(1) << "Physical RAM size: " << memory_size_mi_b;

    const int min_api_level_vm_heap_size = AndroidHwConfigGetMinVmHeapSize(hw, api_level);
    const int min_ram_vm_heap_size = hw.hw_ramSize / 4;
    int min_vm_heap_size = std::max(min_ram_vm_heap_size, min_api_level_vm_heap_size);
    const int max_vm_heap_size = std::min(576, 4 * min_vm_heap_size);

    min_vm_heap_size = std::min(min_vm_heap_size, max_vm_heap_size);

    if (hw.vm_heapSize < min_vm_heap_size) {
        LOG(INFO) << "VM heap size " << hw.vm_heapSize
                  << "MB is below hardware specified minimum of " << min_vm_heap_size
                  << "MB, setting it to that value";

        hw.vm_heapSize = min_vm_heap_size;

        const int min_ram_size = min_vm_heap_size * 2;
        if (hw.hw_ramSize < min_ram_size) {
            hw.hw_ramSize = min_ram_size;
            LOG(INFO) << "Increasing RAM to " << min_ram_size << "MB to accommodate min VM heap";
        }
    }

    if (hw.vm_heapSize > max_vm_heap_size) {
        LOG(INFO) << "VM heap size " << hw.vm_heapSize << "MB is above maximum supported "
                  << max_vm_heap_size << "MB, setting it to that value";
        hw.vm_heapSize = max_vm_heap_size;
    }

    return absl::OkStatus();
}

}  // namespace android::goldfish

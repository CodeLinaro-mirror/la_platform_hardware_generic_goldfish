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
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

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

int AndroidHwConfigGetMinVmHeapSize(const HardwareConfig& config, int api_level) {
    int min_vm_heap_size = 16;
    const hwLcd_screenSize_t screen_size = AndroidHwConfigGetScreenSize(config);
    // Taken from requirements in CDD Documents on VM/Runtime Compatibility
    // TODO: android wear minimums
    if (api_level >= 23) {
        if (screen_size >= LCD_SIZE_XLARGE) {
            // TODO(zyy): emulator currently is unable to allocate a heap this
            // big, so it gets reduced to 576 in main-common.c
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 768;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 576;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_420DPI) {
                // Includes LCD_DENSITY_440DPI
                min_vm_heap_size = 336;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 288;
            } else if (config.hw_lcd_density >= LCD_DENSITY_360DPI) {
                min_vm_heap_size = 240;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI) {
                min_vm_heap_size = 144;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_MDPI) {
                min_vm_heap_size = 80;
            } else {
                min_vm_heap_size = 48;
            }
        } else if (screen_size >= LCD_SIZE_LARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 512;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_420DPI) {
                // Includes LCD_DENSITY_440DPI
                min_vm_heap_size = 228;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_360DPI) {
                min_vm_heap_size = 160;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 80;
            } else if (config.hw_lcd_density >= LCD_DENSITY_MDPI) {
                min_vm_heap_size = 48;
            } else {
                min_vm_heap_size = 32;
            }
        } else {  // screen_size >= LCD_SIZE_SMALL
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_420DPI) {
                // Includes LCD_DENSITY_440DPI
                min_vm_heap_size = 112;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_360DPI ||
                       config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 80;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI ||
                       config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 48;
            } else {  // Includes LCD_DENSITY_MDPI
                min_vm_heap_size = 32;
            }
        }
    } else if (api_level >= 22) {
        if (screen_size >= LCD_SIZE_XLARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 768;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 576;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 288;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI) {
                min_vm_heap_size = 144;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_MDPI) {
                min_vm_heap_size = 80;
            } else {
                min_vm_heap_size = 48;
            }
        } else if (screen_size >= LCD_SIZE_LARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 512;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 80;
            } else if (config.hw_lcd_density >= LCD_DENSITY_MDPI) {
                min_vm_heap_size = 48;
            } else {
                min_vm_heap_size = 32;
            }
        } else {  // screen_size >= LCD_SIZE_SMALL
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 80;
            } else if (config.hw_lcd_density >= LCD_DENSITY_280DPI ||
                       config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 48;
            } else {  // Includes LCD_DENSITY_MDPI
                min_vm_heap_size = 32;
            }
        }
    } else if (api_level >= 21) {
        if (screen_size >= LCD_SIZE_XLARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 768;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 576;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 288;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 96;
            } else {
                min_vm_heap_size = 64;
            }
        } else if (screen_size >= LCD_SIZE_LARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 512;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 384;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 64;
            } else if (config.hw_lcd_density >= LCD_DENSITY_MDPI) {
                min_vm_heap_size = 32;
            } else {
                min_vm_heap_size = 16;
            }
        } else {  // screen_size >= LCD_SIZE_SMALL
            if (config.hw_lcd_density >= LCD_DENSITY_XXXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_560DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 64;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 32;
            } else {  // Includes LCD_DENSITY_MDPI
                min_vm_heap_size = 16;
            }
        }
    } else if (api_level >= 19) {
        if (screen_size >= LCD_SIZE_XLARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 256;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 192;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 64;
            } else {
                min_vm_heap_size = 32;
            }
        } else {  // screen_size >= LCD_SIZE_SMALL
            if (config.hw_lcd_density >= LCD_DENSITY_XXHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_400DPI) {
                min_vm_heap_size = 96;
            } else if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 64;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 32;
            } else {  // Includes LCD_DENSITY_MDPI
                min_vm_heap_size = 16;
            }
        }
    } else if (api_level >= 14) {
        if (screen_size >= LCD_SIZE_XLARGE) {
            if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 128;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 64;
            } else {
                min_vm_heap_size = 32;
            }
        } else {  // screen_size >= LCD_SIZE_SMALL
            if (config.hw_lcd_density >= LCD_DENSITY_XHDPI) {
                min_vm_heap_size = 64;
            } else if (config.hw_lcd_density >= LCD_DENSITY_HDPI ||
                       config.hw_lcd_density >= LCD_DENSITY_TVDPI) {
                min_vm_heap_size = 32;
            } else {  // Includes LCD_DENSITY_MDPI
                min_vm_heap_size = 16;
            }
        }
    } else if (api_level >= 7) {
        if (config.hw_lcd_density >= 240) {
            min_vm_heap_size = 24;
        } else {
            min_vm_heap_size = 16;
        }
    } else {
        min_vm_heap_size = 16;
    }
    return min_vm_heap_size;
}
}  // namespace

absl::Status MemoryConfig::FinalizeRamAndHeapSize(HardwareConfig& hw, int api_level) {
    int ram_size = hw.hw_ramSize;
    if (ram_size <= 0) {
        LOG(WARNING) << "RAM size not specified in AVD, defaulting to 2GiB";
        ram_size = 2048;
    }
    auto memory_size_mi_b = static_cast<uint64_t>(ram_size);

    // enforce CDD minimums
    int min_ram = 32;
    const bool is_foldable = hw.hw_sensor_hinge;
    const bool is_large_screen =
            (static_cast<long long>(hw.hw_lcd_width) * static_cast<long long>(hw.hw_lcd_height)) >=
            3LL * 1000LL * 1000LL;

    if (api_level >= 34) {
        min_ram = 2560;  // 2.5G is required for U and up, to avoid kswapd eating
                         // cpus
        if ((is_foldable || is_large_screen) && min_ram < 3072) {
            min_ram = 3072;  // 3G is required for U and up, to avoid kswapd eating cpus
            LOG(INFO) << "foldable or large screen devices with api >=34 is set to have minimum "
                         "ram 3G";
        }
    }

    if (std::cmp_less(memory_size_mi_b, min_ram)) {
        LOG(INFO) << "Increasing RAM size to " << min_ram << "MB";
        memory_size_mi_b = min_ram;
    }

    hw.hw_ramSize = static_cast<int>(memory_size_mi_b);
    LOG(INFO) << "Physical RAM size: " << memory_size_mi_b;

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

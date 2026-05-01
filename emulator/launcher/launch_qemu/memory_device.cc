
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

#include "memory_device.h"

#include <initializer_list>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/memory_config.h"
#include "goldfish/file/file.h"
#include "goldfish/file/storage_capacity.h"

namespace android::goldfish {

absl::Status MemoryDevice::initialize(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    auto hw = avd.Hw();
    mMemorySizeMiB = hw.hw_ramSize;
    if (mMemorySizeMiB <= 0) {
        LOG(WARNING) << "RAM size not specified in AVD, defaulting to 2GiB";
        mMemorySizeMiB = 2048;
    }
    if (emulator.opts().memory != nullptr) {
        if (!absl::SimpleAtoi(emulator.opts().memory, &mMemorySizeMiB)) {
            return absl::InvalidArgumentError(
                    absl::StrCat("Failed to parse -memory flag: ", emulator.opts().memory));
        }
    }

    int minRam = MemoryConfig::CalculateMinimumRam(hw, avd.ApiLevel(), avd.GetDeviceType());

    if (emulator.opts().lowram) {
        LOG(INFO) << "Resetting min RAM to 0; [reason='lowram_flag_active', api_level="
                  << avd.ApiLevel() << ", device_type=" << static_cast<int>(avd.GetDeviceType())
                  << "]";
        minRam = 0;
    }

    if (mMemorySizeMiB < minRam) {
        LOG(INFO) << "Enforcing minimum RAM requirement; "
                  << "[reason='api_or_device_constraint', "
                  << "original_mib=" << mMemorySizeMiB << ", "
                  << "target_mib=" << minRam << ", "
                  << "api_level=" << avd.ApiLevel() << ", "
                  << "device_type=" << static_cast<int>(avd.GetDeviceType()) << "]";
        mMemorySizeMiB = minRam;
    }

    // TODO re-enable space checking when snapshots are supported.
    /*auto ram = StorageCapacity(mMemorySizeMiB, StorageCapacity::Unit::MiB);

    auto path = avd.getContentPath() / "default_boot";
    if (!base::file::exists(path)) {
        // Lets create it
        if (!base::file::mkdir_recursive(path, 0755)) {
            return absl::DataLossError("Failed to create directory: " + path.string());
        }

#ifdef __linux__
        base::Command::create({"chattr", "+C", path}).execute();
#endif
    }

    auto ram_file = path / "ram.bin";
    StorageCapacity filePageSize = System::GetFilePageSizeForPath(ram_file.c_str());

#ifdef _WIN32
    auto ramSizeBytesWithAlign = ram.align(filePageSize) + filePageSize;
#else
    StorageCapacity ramSizeBytesWithAlign = ram.align(filePageSize);
#endif

    StorageCapacity existingSize;
    //
    // Address the case where there was a previous ram.img there
    // and RAM size was reconfigured.
    System::get()->pathFileSize(ram_file, &existingSize);

    if (existingSize != ramSizeBytesWithAlign) {
        LOG(INFO) << "Insufficient space in existing memory mapped file '" << ram_file
                  << "'. Required size: " << ramSizeBytesWithAlign
                  << " bytes. Existing size: " << existingSize << " bytes. Deleting existing file.";
        base::file::rm(ram_file).IgnoreError();
        existingSize = 0_KiB;
    }
    System::FileSize availableSpace;
    if (!System::get()->pathFreeSpace(path, &availableSpace)) {
        return absl::InternalError("Unable to determine free space for directory: " +
                                   path.string());
    }

    constexpr System::FileSize kSafetyFactor = System::kDiskPressureLimit;
    auto requiredFreeSpace = ramSizeBytesWithAlign - existingSize;

    if (availableSpace < requiredFreeSpace + kSafetyFactor) {
        return absl::ResourceExhaustedError(
                absl::StrFormat("Insufficient space available. Need: %s, available: %s",
                                requiredFreeSpace.string(), availableSpace.string()));
    }*/

    return absl::OkStatus();
}

std::vector<std::string> MemoryDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    auto hw = emulator.avd().Hw();
    return {
        "-m", std::to_string(mMemorySizeMiB)
        //  ,"-object",
        // absl::StrFormat("memory-backend-file,id=android.ram,size=%dM,mem-path=%s,"
        //                 "prealloc=on,share=on",
        //                 mMemorySizeMiB,
        //                 avd->getMemoryMappedDirectory() / "ram.bin"
        //)
    };
}

}  // namespace android::goldfish

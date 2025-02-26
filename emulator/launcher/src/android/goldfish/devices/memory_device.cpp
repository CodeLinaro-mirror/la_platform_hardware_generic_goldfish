
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

#include <android/base/system/System.h>
#include <android/base/system/storage_capacity.h>

#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "aemu/base/process/Command.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {
using base::StorageCapacity;
using base::System;
using base::operator""_KiB;

absl::Status MemoryDevice::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    auto hw = avd.hw();

    auto ram = StorageCapacity(hw.hw_ramSize, StorageCapacity::Unit::MiB);

    auto path = avd.getContentPath() / "default_boot";
    if (!fs::exists(path)) {
        // Lets create it
        if (!std::filesystem::create_directories(path)) {
            return absl::DataLossError("Failed to create directory: " + path.string());
        }

#ifdef __linux__
        base::Command::create({"chattr", "+C", path}).execute();
#endif
    }

    auto ram_file = path / "ram.bin";
    StorageCapacity filePageSize = System::getFilePageSizeForPath(ram_file.c_str());

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
        fs::remove(ram_file);
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
    }

    return absl::OkStatus();
}

std::vector<std::string> MemoryDevice::getQemuParameters(const Emulator& emulator) const {
    auto hw = emulator.avd().hw();
    return {
            "-m", std::to_string(hw.hw_ramSize)
            //  ,"-object",
            // absl::StrFormat("memory-backend-file,id=android.ram,size=%dM,mem-path=%s,"
            //                 "prealloc=on,share=on",
            //                 hw.hw_ramSize,
            //                 avd->getMemoryMappedDirectory() / "ram.bin"
            //)
    };
}

}  // namespace android::goldfish
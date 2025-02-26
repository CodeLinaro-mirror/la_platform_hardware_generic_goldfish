
// Copyright (C) 2024 The Android Open Source Project
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
#include "disk_drive.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string_view>

#include "absl/log/absl_log.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include "aemu/base/process/Command.h"
#include "aemu/base/process/Process.h"
#include "android/base/system/System.h"
#include "android/base/system/storage_capacity.h"
#include "android/filesystems/ext4_resize.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"

namespace android::goldfish {
using android::base::System;
using android::base::operator""_MiB;
using android::base::operator""_TiB;

absl::Status RawDrive::initialize(const Emulator& emulator) {
    return emulator.avd().getSystemImageFilePath(mImage).status();
};

std::vector<std::string> RawDrive::getQemuParameters(const Emulator& emulator) const {
    const Avd& avd = emulator.avd();
    auto diskImage = avd.getSystemImageFilePath(mImage);
    auto diskId = diskImage->filename().string();
    return {"-device", absl::StrFormat("virtio-blk,addr=%s,drive=%s,num-queues=4", addr(), diskId),
            "-blockdev",
            absl::StrFormat("driver=raw,node-name=%s,read-only=on,driver="
                            "file,filename=%s",
                            diskId, diskImage->string())};
}

void MutableDiskDrive::clear() {
    fs::remove(mDiskImage);
    auto img = mDiskImage;
    // assert(img.extension() == ".qcow2");
    fs::remove(img.replace_extension());
}

std::vector<std::string> MutableDiskDrive::getQemuParameters(const Emulator& emulator) const {
    // TODO(jansene): Optimize for performance.
    // For example run an individual iothread per drive
    //  "-object",  "iothread,id=disk-iothread" per drive..
    // and setup proper caching.
    return {"-device", absl::StrFormat("virtio-blk,addr=%s,drive=%s,num-queues=4", addr(), mDiskId),
            "-blockdev",
            absl::StrFormat("driver=qcow2,node-name=%s,file.driver=file,file.filename=%s", mDiskId,
                            mDiskImage.string())};
}

absl::Status MutableDiskDrive::createExt4Image(fs::path destination, StorageCapacity size,
                                               std::string mount_point) {
    if (android_createEmptyExt4Image(destination.string().c_str(), size.bytes(),
                                     mount_point.c_str()) == 0) {
        return absl::OkStatus();
    }

    return absl::InternalError(
            absl::StrFormat("Failed to create an empty Ext4 image in '%s' of size %d bytes",
                            destination.string(), size.bytes()));
}

absl::Status MutableDiskDrive::resizePartition(fs::path partition, StorageCapacity size) {
    constexpr auto minSize = 128_MiB;
    constexpr auto maxSize = 16_TiB;

    if (size < minSize) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Partition '%s' cannot be smaller than %s. Requested size: %s",
                                partition.string(), minSize.string(), size.string()));
    }

    if (size > maxSize) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Partition '%s' cannot be larger than %s. Requested size: %s",
                                partition.string(), maxSize.string(), size.string()));
    }

    int resizeResult = resizeExt4Partition(partition.string().c_str(), size.bytes());

    // Interpret the error codes can propagate.
    if (resizeResult != 0) {
        std::string resizeError;
        switch (resizeResult) {
            case -1:
                resizeError = "Argument formatting failed";
                break;
            case -2:
                resizeError = "System call failed";
                break;
            default:
                resizeError = absl::StrFormat("resize2fs failed with exit code %d", resizeResult);
                break;
        }
        return absl::InternalError(absl::StrFormat("Could not resize partition %s. Error: %s",
                                                   partition.string(), resizeError));
    }

    return absl::OkStatus();
}

absl::Status MutableDiskDrive::convertImgToQcow2(fs::path ext4_image) {
    constexpr auto kQemuImgTimeout = std::chrono::seconds(10);

    if (!fs::exists(ext4_image)) {
        return absl::NotFoundError(
                absl::StrFormat("The path: %s does not exist.", ext4_image.string()));
    }

    auto startTime = std::chrono::steady_clock::now();
    auto qemu_img = System::get()->findBundledExecutable("qemu-img");
    if (!fs::exists(qemu_img)) {
        return absl::NotFoundError(
                "The bundled executable qemu-img cannot be "
                "found, please check you installation.");
    }
    std::string qcow2 = ext4_image.string();
    qcow2 += ".qcow2";
    auto img_proc = base::Command::create({qemu_img.string(), "convert", "-O", "qcow2",
                                           ext4_image.string(), qcow2})
                            .execute();
    if (img_proc->wait_for(kQemuImgTimeout) == std::future_status::timeout) {
        return absl::DeadlineExceededError(
                absl::StrFormat("Failed to convert %s to %s in %d seconds.", ext4_image.string(),
                                qcow2, kQemuImgTimeout.count()));
    }
    if (!System::get()->pathIsQcow2(qcow2)) {
        return absl::DataLossError(
                absl::StrFormat("The created file %s is not in qcow2 format", qcow2));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);
    long long timeUsedMs = (long long)elapsed.count();
    ABSL_VLOG(1) << "Converted ext4->qcow2 " << ext4_image << " to " << qcow2 << " in "
                 << timeUsedMs << " milliseconds.";
    return absl::OkStatus();
}

absl::Status MutableDiskDrive::createExt4ImageFromDirectory(fs::path source, fs::path destination,
                                                            StorageCapacity size,
                                                            std::string mount_point) {
    if (android_createExt4ImageFromDir(destination.string().c_str(), source.string().c_str(),
                                       size.bytes(), mount_point.c_str()) == 0) {
        return absl::OkStatus();
    }

    return absl::InternalError(
            absl::StrFormat("Failed to create Ext4 image from directory '%s' to '%s'",
                            source.string(), destination.string()));
}

}  // namespace android::goldfish
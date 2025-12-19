
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
#include <fstream>
#include <future>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/process/command.h"
#include "aemu/base/utils/status_macros.h"
#include "android/base/file/file.h"
#include "android/filesystems/ext4_utils.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

namespace {
std::string getDeviceParam(const Avd& avd, std::string_view diskId, std::string_view addr) {
    switch (avd.detectArchitecture()) {
    case Avd::CpuArchitecture::kArm:
        // Note that this isn't actually a pci device, oh well.
        return absl::StrCat("virtio-blk-device,drive=", diskId);
    case Avd::CpuArchitecture::kX86:
        return absl::StrCat("virtio-blk-pci,addr=", addr, ",drive=", diskId);
    case Avd::CpuArchitecture::kRiscV:
    case Avd::CpuArchitecture::kUnknown:
    default:
        return {};
    }
}

absl::Status createExt4Image(fs::path destination, StorageCapacity size, std::string mount_point) {
    if (android::filesystems::android_createEmptyExt4Image(destination, size.bytes(),
                                                           mount_point.c_str()) == 0) {
        return absl::OkStatus();
    }

    return absl::InternalError(
            absl::StrFormat("Failed to create an empty Ext4 image in '%s' of size %d bytes",
                            destination.string(), size.bytes()));
}

bool pathIsQcow2(fs::path path) {
    // read 4 bytes
    uint8_t magic[4] = {'\0'};
    std::ifstream ifs(path, std::ios_base::binary);
    if (!ifs.good()) {
        return false;
    }
    ifs.read(reinterpret_cast<char*>(magic), sizeof(magic));

    bool matched4bytes = false;
    if (magic[0] == 'Q' && magic[1] == 'F' && magic[2] == 'I' &&
        magic[3] == static_cast<uint8_t>('\xfb')) {
        matched4bytes = true;
    }

    return matched4bytes;
}

absl::Status convertImgToQcow2(const fs::path& qemu_img_binary, fs::path ext4_image,
                               fs::path qcow2_image) {
    constexpr auto kQemuImgTimeout = std::chrono::seconds(10);

    if (!base::file::exists(ext4_image)) {
        return absl::NotFoundError(
                absl::StrFormat("The path: %s does not exist.", ext4_image.string()));
    }

    auto startTime = std::chrono::steady_clock::now();

    VLOG(1) << "Running: " << qemu_img_binary.string() << " convert -O qcow2 "
            << ext4_image.string() << " " << qcow2_image.string();
    auto img_proc = base::Command::create({qemu_img_binary.string(), "convert", "-O", "qcow2",
                                           ext4_image.string(), qcow2_image.string()})
                            .execute();
    if (img_proc->wait_for(kQemuImgTimeout) == std::future_status::timeout) {
        return absl::DeadlineExceededError(
                absl::StrFormat("Failed to convert %s to %s in %d seconds.", ext4_image.string(),
                                qcow2_image.string(), kQemuImgTimeout.count()));
    }
    if (img_proc->exitCode() != 0) {
        return absl::InternalError(absl::StrCat(
                "qemu-img reported qcow2 creation failed with exit code ", img_proc->exitCode(),
                ": ", ext4_image.string(), " -> ", qcow2_image.string()));
    }
    if (!base::file::exists(qcow2_image)) {
        return absl::NotFoundError(absl::StrCat("The requested qcow2 file has not been created: ",
                                                qcow2_image.string()));
    }
    if (!pathIsQcow2(qcow2_image)) {
        return absl::DataLossError(
                absl::StrFormat("The created file %s is not in qcow2 format", qcow2_image));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);
    long long timeUsedMs = (long long)elapsed.count();
    VLOG(1) << "Converted ext4->qcow2 " << ext4_image << " to " << qcow2_image << " in "
            << timeUsedMs << " milliseconds.";
    return absl::OkStatus();
}

}  // namespace

absl::Status RoDrive::initialize(const EmulatorConfig& emulator) {
    if (!base::file::is_file(mImagePath)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Unable to initialize drive as image isn't a file: ", mImagePath.string()));
    }
    if (!base::file::can_read(mImagePath)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Unable to initialize drive as image file can't be read: ", mImagePath.string()));
    }
    return absl::OkStatus();
}

std::vector<std::string> RoDrive::getQemuParameters(const EmulatorConfig& emulator) const {
    const Avd& avd = emulator.avd();
    return {"-device", getDeviceParam(avd, id(), addr()), "-blockdev",
            absl::StrCat("driver=raw,node-name=", id(),
                         ",read-only=on,driver=file,filename=", mImagePath.string())};
}

absl::Status RwDrive::initialize(const EmulatorConfig& emulator) {
    if (mWipeExisting) {
        base::file::rm(mQcow2Image).IgnoreError();
        base::file::rm(mDestinationImage).IgnoreError();
    }

    if (!base::file::exists(mDestinationImage)) {
        base::file::rm(mQcow2Image).IgnoreError();
        if (mSourcePath) {
            base::file::cp_file(*mSourcePath, mDestinationImage, /*overwrite=*/true).IgnoreError();

            if (!base::file::exists(mDestinationImage)) {
                return absl::NotFoundError(absl::StrCat("Failed to copy '", mSourcePath->string(),
                                                        "' to '", mDestinationImage.string(), "'"));
            }
        } else {
            LOG(INFO) << "Preparing empty drive: " << mDestinationImage;
            RETURN_IF_ERROR(createExt4Image(mDestinationImage, mSizeBytes, id()));
        }
    }

    if (!base::file::exists(mQcow2Image)) {
        return convertImgToQcow2(emulator.paths().qemu_img_binary, mDestinationImage, mQcow2Image);
    }

    return absl::OkStatus();
}

std::vector<std::string> RwDrive::getQemuParameters(const EmulatorConfig& emulator) const {
    return {
        "-device", absl::StrCat(getDeviceParam(emulator.avd(), id(), addr()), ",write-cache=on"),
        "-blockdev",
        absl::StrCat(
                "driver=qcow2,node-name=", id(),
                ",file.driver=file,file.filename=", mQcow2Image.string(),
                ",overlap-check=none,cache.direct=off,cache.no-flush=on,l2-cache-size=1048576")};
}

}  // namespace android::goldfish

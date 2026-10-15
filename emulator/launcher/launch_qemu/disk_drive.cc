
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

#include <filesystem>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/fsimage/ext4.h"
#include "goldfish/fsimage/qcow2.h"

namespace android::goldfish {

namespace {
std::string getDeviceParam(const Avd& avd, std::string_view diskId, std::string_view addr) {
    switch (avd.Arch()) {
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

}  // namespace

std::vector<std::string> NullDrive::getQemuParameters(const EmulatorConfig& emulator) const {
    const Avd& avd = emulator.avd();
    return {"-device", getDeviceParam(avd, id(), addr()), "-blockdev",
            absl::StrCat("driver=null-co,node-name=", id(), ",read-only=on,read-zeroes=on")};
}

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
    return {
        "-device", getDeviceParam(avd, id(), addr()), "-blockdev",
        absl::StrCat("driver=raw,node-name=", id(), ",read-only=on,file.driver=file,file.filename=",
                     EscapeQemuOptionValue(mImagePath.string()))};
}

std::vector<std::string> Qcow2Drive::getQemuParameters(const EmulatorConfig& emulator) const {
    return {
        "-device", absl::StrCat(getDeviceParam(emulator.avd(), id(), addr()), ",write-cache=on"),
        "-blockdev",
        absl::StrCat(
                "driver=qcow2,node-name=", id(),
                ",file.driver=file,file.filename=", EscapeQemuOptionValue(qcow2_image_.string()),
                ",overlap-check=none,cache.direct=off,cache.no-flush=on,l2-cache-size=1048576")};
}

absl::Status Qcow2Drive::MakeQcow2() const {
    if (!base::file::exists(qcow2_image_)) {
        return ::goldfish::fsimage::CreateQcow2Overlay(qcow2_image_, backing_image_);
    }

    return absl::OkStatus();
}

absl::Status WritableImgDrive::initialize(const EmulatorConfig& emulator) {
    return MakeQcow2();
}

absl::Status BlankWritableDrive::initialize(const EmulatorConfig& emulator) {
    if (base::file::exists(backing_image_)) {
        LOG(INFO) << backing_image_ << " already exists for " << id();
    } else {
        LOG(INFO) << "Preparing empty drive: " << backing_image_;
        base::file::rm(qcow2_image_).IgnoreError();
        RETURN_IF_ERROR(::goldfish::fsimage::CreateEmptyExt4Image(backing_image_, size_, id()));
    }

    return MakeQcow2();
}

absl::Status WritableDirectoryDrive::initialize(const EmulatorConfig& emulator) {
    if (base::file::exists(backing_image_)) {
        LOG(INFO) << backing_image_ << " already exists for " << id();
    } else {
        LOG(INFO) << "Preparing non empty drive: " << backing_image_ << " from src directory "
                  << src_directory_;
        base::file::rm(qcow2_image_).IgnoreError();
        RETURN_IF_ERROR(::goldfish::fsimage::CreateExt4ImageFromDir(backing_image_, src_directory_,
                                                                    size_, id()));
    }

    return MakeQcow2();
}

}  // namespace android::goldfish

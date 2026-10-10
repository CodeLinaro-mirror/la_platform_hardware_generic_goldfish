
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
#pragma once

/* a macro used to define the list of disk images managed by the
 * implementation. This macro will be expanded several times with
 * varying definitions of _AVD_IMG
 */
// NOLINTBEGIN
#define AVD_IMAGE_LIST                                                                         \
    _AVD_IMG(KERNELRANCHU, "kernel-ranchu", "kernel")                                          \
    _AVD_IMG(RAMDISK, "ramdisk.img", "ramdisk")                                                \
    _AVD_IMG(SYSTEM, "system.img", "system")                                                   \
    _AVD_IMG(VENDOR, "vendor.img", "vendor")                                                   \
    _AVD_IMG(USERDATA, "userdata.img", "user data")                                            \
    _AVD_IMG(USERDATA_OLD, "userdata-qemu.img", "user data (old name)")                        \
    _AVD_IMG(CACHE, "cache.img", "cache")                                                      \
    _AVD_IMG(SDCARD, "sdcard.img", "SD Card (survives -wipe-data)")                            \
    _AVD_IMG(SDCARD_EMU, "sdcard.emu.img", "SD Card (created by emulator)")                    \
    _AVD_IMG(SDCARD_EXTRNL_QCOW2, "sdcard.external.qcow2", "QCOW2 for SD Card outside of AVD") \
    _AVD_IMG(ENCRYPTIONKEY, "encryptionkey.img", "Encryption Key")                             \
    _AVD_IMG(VERIFIEDBOOTPARAMS, "VerifiedBootParams.textproto", "Verified Boot Parameters")   \
    _AVD_IMG(KERNELCOMMANDLINE, "kernel_cmdline.txt", "kernel command line option")            \
    _AVD_IMG(BUILDPROP, "build.prop", "Build properties")
// NOLINTEND
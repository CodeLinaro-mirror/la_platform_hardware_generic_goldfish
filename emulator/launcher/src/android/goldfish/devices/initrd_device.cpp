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
#include "initrd_device.h"

#include <assert.h>
#include <stddef.h>

#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/base/system/System.h"
#include "android/emulation/control/adb/adbkey.h"
#include "android/goldfish/bootconfig.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"
#include "drives/disk_drive.h"

namespace android::goldfish {

// Note: The ACPI _HID that follows devices/ must match the one defined in the
// ACPI tables (hw/i386/acpi_build.c)
constexpr std::string_view kSysfsAndroidDtDir =
        "/sys/bus/platform/devices/ANDR0001:00/properties/android/";
constexpr std::string_view kSysfsAndroidDtDirDtb = "/proc/device-tree/firmware/android/";

// using android::base::splitTokens;
// using android::base::absl::StrFormat;

static std::string getDeviceStateString(const HardwareConfig& hw) {
    // TODO(jansene): Foldable support.
    return "";
}

std::vector<std::pair<std::string, std::string>> getUserspaceBootProperties(
        std::string targetArch, std::string serialno, const int bootPropOpenglesVersion,
        const int apiLevel, std::string kernelSerialPrefix,
        const std::vector<std::string>& verifiedBootParameters, const HardwareConfig& hw) {
    const bool isX86ish = targetArch == "x86" || targetArch == "x86_64";
    const bool hasShellConsole = false;
    std::string androidbootVerityMode = "androidboot.veritymode";
    std::string checkjniProp = "androidboot.dalvik.vm.checkjni";
    std::string bootanimProp = "androidboot.debug.sf.nobootanimation";
    std::string bootanimPropValue = "1";
    std::string qemuScreenOffTimeoutProp = "androidboot.qemu.settings.system.screen_off_timeout";
    std::string qemuVsyncProp = "androidboot.qemu.vsync";
    std::string qemuGltransportNameProp = "androidboot.qemu.gltransport.name";
    std::string hwGltransportNameProp = "androidboot.hardware.gltransport";
    std::string qemuDrawFlushIntervalProp = "androidboot.qemu.gltransport.drawFlushInterval";
    std::string qemuOpenglesVersionProp = "androidboot.opengles.version";
    std::string qemuUirendererProp = "androidboot.debug.hwui.renderer";
    std::string dalvikVmHeapsizeProp = "androidboot.dalvik.vm.heapsize";
    std::string qemuLegacyFakeCameraProp = "androidboot.qemu.legacy_fake_camera";
    std::string qemuCameraProtocolVerProp = "androidboot.qemu.camera_protocol_ver";
    std::string qemuCameraHqEdgeProp = "androidboot.qemu.camera_hq_edge_processing";
    std::string qemuDisplaySettingsXmlProp = "androidboot.qemu.display.settings.xml";
    std::string qemuVirtioWifiProp = "androidboot.qemu.virtiowifi";
    std::string qemuWifiProp = "androidboot.qemu.wifi";
    std::string qemuHwcodecAvcdecProp = "androidboot.qemu.hwcodec.avcdec";
    std::string qemuHwcodecHevcdecProp = "androidboot.qemu.hwcodec.hevcdec";
    std::string qemuHwcodecVpxdecProp = "androidboot.qemu.hwcodec.vpxdec";
    std::string androidbootLogcatProp = "androidboot.logcat";
    std::string adbKeyProp = "androidboot.qemu.adb.pubkey";
    std::string avdNameProp = "androidboot.qemu.avd_name";
    std::string deviceStateProp = "androidboot.qemu.device_state";
    std::string qemuCpuVulkanVersionProp = "androidboot.qemu.cpuvulkan.version";
    std::string emulatorCircularProp = "androidboot.emulator.circular";
    std::string autoRotateProp = "androidboot.qemu.autorotate";
    std::string qemuExternalDisplays = "androidboot.qemu.external.displays";
    std::vector<std::pair<std::string, std::string>> params;

    params.push_back({"qemu.logcat_filter", "*:V"});
    params.push_back({"androidboot.qemu", "1"});
    params.push_back({"androidboot.hardware", "ranchu"});
    if (!serialno.empty()) {
        params.push_back({"androidboot.serialno", serialno});
    }

    //   if (opts->guest_angle) {
    //     params.push_back({"androidboot.hardwareegl", "angle"});
    //   }

    //   if (fc::isEnabled(fc::Vulkan)) {
    //   params.push_back({"androidboot.hardware.vulkan", "ranchu"});
    //   }

    // Put our swiftshader version string there, which is currently
    // Vulkan 1.1 (0x402000)
    params.push_back({qemuCpuVulkanVersionProp, absl::StrFormat("%d", 0x402000)});

    // Always on.
    params.push_back({qemuScreenOffTimeoutProp, "2147483647"});
    params.push_back({androidbootVerityMode, "enforcing"});

    // Set vsync rate
    params.push_back({qemuVsyncProp, absl::StrFormat("%u", hw.hw_lcd_vsync)});

    // Set gl transport props
    params.push_back({qemuGltransportNameProp, hw.hw_gltransport});
    params.push_back({hwGltransportNameProp, hw.hw_gltransport});

    params.push_back({qemuDrawFlushIntervalProp,
                      absl::StrFormat("%u", hw.hw_gltransport_drawFlushInterval)});

    // OpenGL ES related setup
    // 1. Set opengles.version and set Skia as UI renderer if
    // GLESDynamicVersion = on (i.e., is a reasonably good driver)
    params.push_back({qemuOpenglesVersionProp, absl::StrFormat("%d", bootPropOpenglesVersion)});

    params.push_back({qemuUirendererProp, "skiagl"});
    params.push_back({androidbootLogcatProp, "*:V"});

    // Send adb public key to device
    auto privkey = getPrivateAdbKeyPath();
    std::string key;

    if (!privkey.empty() && pubkey_from_privkey(privkey, &key)) {
        params.push_back({adbKeyProp, key});
    } else {
        LOG(WARNING) << "No adb private key exists";
    }

    // if (opts->bootchart) {
    //   params.push_back({"androidboot.bootchart", opts->bootchart});
    // }

    // if (opts->selinux) {
    //   params.push_back({"androidboot.selinux", opts->selinux});
    // }

    if (hw.vm_heapSize > 0) {
        params.push_back({dalvikVmHeapsizeProp, absl::StrFormat("%dm", hw.vm_heapSize)});
    }

    // Camera config.
    // if (opts->legacy_fake_camera) {
    //   params.push_back({qemuLegacyFakeCameraProp, "1"});
    // }

    // if (!opts->camera_hq_edge) {
    //   params.push_back({qemuCameraHqEdgeProp, "0"});
    // }

    params.push_back({qemuCameraProtocolVerProp, "1"});

    if (isX86ish) {
        // x86 and x86_64 platforms use an alternative Android DT directory that
        // mimics the layout of /proc/device-tree/firmware/android/
        params.push_back({"androidboot.android_dt_dir", std::string(kSysfsAndroidDtDir)});
    }

    for (const std::string& param : verifiedBootParameters) {
        const size_t i = param.find('=');
        if (i == std::string::npos) {
            params.push_back({param, ""});
        } else {
            params.push_back({param.substr(0, i), param.substr(i + 1)});
        }
    }

    // display settings file name
    if (!hw.display_settings_xml.empty()) {
        params.push_back({qemuDisplaySettingsXmlProp, hw.display_settings_xml});
    }

    params.push_back({qemuVirtioWifiProp, "1"});

    // Hardware decoder
    params.push_back({qemuHwcodecAvcdecProp, "2"});
    params.push_back({qemuHwcodecHevcdecProp, "2"});
    params.push_back({qemuHwcodecVpxdecProp, "2"});

    if (hasShellConsole) {
        params.push_back({"androidboot.console", absl::StrFormat("%s0", kernelSerialPrefix)});
    }

    params.push_back({avdNameProp, hw.avd_name});

    std::string deviceState = getDeviceStateString(hw);
    if (!deviceState.empty()) {
        LOG(INFO) << " sending device_state_config:" << deviceState;
        params.push_back({deviceStateProp, deviceState});
    }

    //   if (fc::isEnabled(fc::SupportPixelFold)) {
    //     if (android_foldable_hinge_configured() &&
    //         android_foldable_is_pixel_fold()) {
    //       int width{0}, height{0};
    //       width = hw.hw_displayRegion_0_1_width;
    //       height = hw.hw_displayRegion_0_1_height;
    //       dinfo("Configuring second built-in display with width %d and "
    //             "height %d for pixel_fold device",
    //             width, height);
    //       std::string display_list =
    //           absl::StrFormat("1,%d,%d,%d,0", width, height,
    //           hw.hw_lcd_density);
    //       params.push_back({qemuExternalDisplays, display_list});
    //     }
    //   }
    //   if (resizableEnabled()) {
    //     params.push_back({qemuDisplaySettingsXmlProp, "resizable"});
    //   }

    //   if (android_foldable_hinge_configured()) {
    //     params.push_back({autoRotateProp, "1"});
    //   }

    //   for (auto i = opts->append_userspace_opt; i; i = i->next) {
    //     std::string const val = i->param;
    //     std::string const eq = strchr(val, '=');
    //     if (eq) {
    //       params.push_back({std::string(val, eq), eq + 1});
    //     } else {
    //       params.push_back({val, ""});
    //     }
    //   }

    if (hw.hw_lcd_circular) {
        params.push_back({emulatorCircularProp, "1"});
    }

    return params;
}

static std::string getDynamicPartitionBootDevice(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    auto arch = avd.detectArchitecture();
    auto drive = emulator.get<PciDevice>("system");

    if (arch == Avd::CpuArchitecture::kX86) {
        return "pci0000:00/0000:00:03.0";
    }

    assert(arch == Avd::CpuArchitecture::kArm);

    // TODO(jansene): We need should determine device id from the order they were
    // added to emulator.
    // "a003e00", "a003c00", "a003a00", "a003800", "a003600", "a003400",
    // system is currently first - "a0003e00".
    // 3c must be encrypt (metadata)
    return "a003e00.virtio_mmio";
}

static std::vector<std::string> getVerifiedBootparams(const Emulator& emulator) {
    // Get verified boot kernel parameters, if they exist.
    // If this is not a playstore image, then -writable_system will
    // disable verified boot

    //   auto avd = emulator.avd();
    std::vector<std::string> verified_boot_params;
    //   //   if (feature_is_enabled(kFeature_PlayStoreImage) ||
    //   //       !android_op_writable_system ||
    //   //       feature_is_enabled(kFeature_DynamicPartition)) {
    //   auto verifiedBootParamsPath =
    //       avd.getImagePath(Avd::ImageType::VERIFIEDBOOTPARAMS);
    //   if (verifiedBootParamsPath.ok()) {
    //     android::verifiedboot::getParametersFromFile(
    //         verifiedBootParamsPath.get(), // NULL here is OK
    //         &verified_boot_params);
    //   }
    //   //   if (feature_is_enabled(kFeature_DynamicPartition)) {
    std::string boot_dev =
            absl::StrCat("androidboot.boot_devices=", getDynamicPartitionBootDevice(emulator));
    verified_boot_params.push_back(boot_dev);
    // }
    // if (android_op_writable_system) {
    // unlocked state

    verified_boot_params.push_back("androidboot.verifiedbootstate=orange");
    return verified_boot_params;
}

absl::Status Initrd::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    auto hw = avd.hw();

    auto init_rd = avd.getContentPath() / "initrd";
    // Use the one provided by hardware config if available
    if (fs::exists(init_rd)) {
        return absl::OkStatus();
    }

    int gles_major_version = 2;
    int gles_minor_version = 0;
    int bootPropOpenglesVersion = gles_major_version << 16 | gles_minor_version;
    std::string real_console_tty_prefix = "hvc";
    int apiLevel = 202504;
    auto verifiedBootParameters = getVerifiedBootparams(emulator);
    auto properties = getUserspaceBootProperties(
            hw.hw_cpu_arch, avd.name(), bootPropOpenglesVersion, apiLevel, real_console_tty_prefix,
            verifiedBootParameters, hw);
    // Ok.. let's create it
    LOG(INFO) << "Creating initrd from " << hw.disk_ramdisk_path << " -> " << init_rd;
    if (::goldfish::createRamdiskWithBootconfig(hw.disk_ramdisk_path.c_str(),
                                                init_rd.string().c_str(), properties) != 0) {
        return absl::InternalError("Failed to create initrd image with bootpropterties.");
    }

    return absl::OkStatus();
}

// TODO(jansene) add Initrd versioning magic to add/subtract parameters,
std::vector<std::string> Initrd::getQemuParameters(const Emulator& emulator) const {
    const Avd& avd = emulator.avd();
    return {"-initrd", android::base::System::pathAsString(avd.getContentPath() / "initrd")};
}
}  // namespace android::goldfish

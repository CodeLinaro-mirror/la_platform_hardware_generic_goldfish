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

#include <stddef.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "android/status/status_macros.h"
#include "bootconfig.h"
#include "goldfish/adb/adbkey.h"
#include "goldfish/file/file.h"
#include "goldfish/sensors/foldable_model.h"

namespace android::goldfish {
namespace {

// Note: The ACPI _HID that follows devices/ must match the one defined in the
// ACPI tables (hw/i386/acpi_build.c)
constexpr std::string_view kSysfsAndroidDtDir =
        "/sys/bus/platform/devices/ANDR0001:00/properties/android/";
constexpr std::string_view kSysfsAndroidDtDirDtb = "/proc/device-tree/firmware/android/";

// using android::base::splitTokens;
// using android::base::absl::StrFormat;

std::string getDeviceStateString(const HardwareConfig& hw) {
    // TODO(jansene): Foldable support.
    return "";
}

std::vector<std::pair<std::string, std::string>> getUserspaceBootProperties(
        std::string targetArch, std::string serialno, const int bootPropOpenglesVersion,
        const int apiLevel, std::string kernelSerialPrefix,
        const std::vector<std::string>& verifiedBootParameters, const Avd& avd,
        const AndroidOptions& opts, const UserPaths& paths) {
    const bool isX86ish = targetArch == "x86" || targetArch == "x86_64";
    const bool hasShellConsole = false;
    std::string androidbootVerityMode = "androidboot.veritymode";
    std::string androidbootHardwareGralloc = "androidboot.hardware.gralloc";
    std::string androidbootQemuSkin = "androidboot.qemu.skin";
    std::string checkjniProp = "androidboot.dalvik.vm.checkjni";
    std::string bootanimProp = "androidboot.debug.sf.nobootanimation";
    std::string bootanimPropValue = "1";
    std::string qemuScreenOffTimeoutProp = "androidboot.qemu.settings.system.screen_off_timeout";
    std::string qemuVsyncProp = "androidboot.qemu.vsync";
    std::string qemuGltransportNameProp = "androidboot.qemu.gltransport.name";
    std::string hwGltransportNameProp = "androidboot.hardware.gltransport";
    std::string hwEglProp = "androidboot.hardwareegl";
    std::string aemuAngleOverridesDisabledProp =
            "androidboot.hardware.aemu_feature_overrides_disabled";
    std::string angleFeatureOverridesDisabledProp =
            "androidboot.hardware.angle_feature_overrides_disabled";
    std::string angleFeatureOverridesEnabledProp =
            "androidboot.hardware.angle_feature_overrides_enabled";
    std::string qemuDrawFlushIntervalProp = "androidboot.qemu.gltransport.drawFlushInterval";
    std::string qemuOpenglesVersionProp = "androidboot.opengles.version";
    std::string qemuUirendererProp = "androidboot.debug.hwui.renderer";
    std::string qemuRenderengineProp = "androidboot.debug.renderengine.backend";
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
    std::string qemuDisplayConfigs0 = "androidboot.qemu.display.0.configs";
    std::string qemuRadioDataInterfaceName = "androidboot.qemu.radio.data_interface_name";

    std::vector<std::pair<std::string, std::string>> params;

    params.push_back({"qemu.logcat_filter", "*:S"});
    params.push_back({"androidboot.qemu", "1"});
    params.push_back({"androidboot.hardware", "ranchu"});

    if (opts.no_boot_anim) {
        params.push_back({bootanimProp, "1"});
        params.push_back({"android.bootanim", "0"});
    }

    if (!serialno.empty()) {
        // playstore does not like _ or . etc, replacing them with X
        // otherwise, it does not allow playstore login
        std::replace_if(
                serialno.begin(), serialno.end(), [](unsigned char c) { return !std::isalnum(c); },
                'X');

        params.push_back({"androidboot.serialno", serialno});
    }

    // Use Guest ANGLE by default
    if (!opts.no_guest_angle) {
        // Enable GuestAngle (ro.hardware.egl = angle).
        params.push_back({hwEglProp, "angle"});

        const char* env_aemu_angle_overrides_disabled =
                std::getenv("AEMU_ANGLE_OVERRIDES_DISABLED");
        std::string aemu_angle_overrides_disabled =
                env_aemu_angle_overrides_disabled ? env_aemu_angle_overrides_disabled : "";

        const char* env_angle_overrides_enabled = std::getenv("ANGLE_FEATURE_OVERRIDES_ENABLED");
        std::string angle_overrides_enabled =
                env_angle_overrides_enabled ? env_angle_overrides_enabled : "";

        const char* env_angle_overrides_disabled = std::getenv("ANGLE_FEATURE_OVERRIDES_DISABLED");
        std::string angle_overrides_disabled =
                env_angle_overrides_disabled ? env_angle_overrides_disabled : "";

        const char* env_vk_icd = std::getenv("ANDROID_EMU_VK_ICD");
        const std::string vk_icd = env_vk_icd ? env_vk_icd : "";
        if ((vk_icd == "lavapipe") && angle_overrides_enabled != "0" &&
            angle_overrides_enabled.find("supportsAndroidNativeFenceSync") == std::string::npos) {
            if (!angle_overrides_enabled.empty()) {
                angle_overrides_enabled += ";";
            }
            angle_overrides_enabled += "supportsAndroidNativeFenceSync";
        }

        if (angle_overrides_disabled.empty()) {
            // TODO(b/515372950): disable supportsBlendOperationAdvanced
            // which is added due to dEQP failures with lavapipe
            angle_overrides_disabled = "supportsBlend*";

            // TODO(b/515372950): detect host gpu before this point to automatically enable
            // appropriate angle overrides.
            const char* env_vk_nvidia = std::getenv("AEMU_VK_NVIDIA");
            bool isVkNVIDIA = env_vk_nvidia && env_vk_nvidia[0] == '1';
            if (isVkNVIDIA) {
                // enablePrecisionQualifiers
                angle_overrides_disabled += ":enablePrec*";
            }

            const int MAX_PARAM_LENGTH = 92;
            if (angle_overrides_disabled.length() > MAX_PARAM_LENGTH) {
                LOG(ERROR) << "Cannot add angle boot parameters!";
            }
        }

        if (aemu_angle_overrides_disabled != "0" && !aemu_angle_overrides_disabled.empty()) {
            params.push_back({aemuAngleOverridesDisabledProp, aemu_angle_overrides_disabled});
        }
        if (angle_overrides_disabled != "0" && !angle_overrides_disabled.empty()) {
            params.push_back({angleFeatureOverridesDisabledProp, angle_overrides_disabled});
        }
        if (angle_overrides_enabled != "0" && !angle_overrides_enabled.empty()) {
            params.push_back({angleFeatureOverridesEnabledProp, angle_overrides_enabled});
        }
    }

    params.push_back({"androidboot.hardware.vulkan", "ranchu"});

    // Put software vulkan driver version, based on software driver version
    // and the CTS requirements
    int vulkanVersion = 0x00402000;  // 1.2
    if (apiLevel >= 37) {
        vulkanVersion = 0x00404000;  // 1.4
    } else if (apiLevel >= 34) {
        vulkanVersion = 0x00403000;  // 1.3
    }
    params.push_back({qemuCpuVulkanVersionProp, absl::StrFormat("%d", vulkanVersion)});

    // Always on.
    params.push_back({qemuScreenOffTimeoutProp, "2147483647"});
    params.push_back({androidbootVerityMode, "enforcing"});

    // only work with minigbm gralloc
    params.push_back({androidbootHardwareGralloc, "minigbm"});

    // setup skin

    auto skinName = avd.SkinName();
    if (!skinName.empty() && isalpha((unsigned char)(skinName[0]))) {
        params.push_back({androidbootQemuSkin, skinName});
    }

    auto hw = avd.Hw();
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

    // Use skiavkthreaded by default, unless user specifies otherwise
    std::string systemui_renderer =
            opts.systemui_renderer ? opts.systemui_renderer : "skiavkthreaded";

    params.push_back({qemuUirendererProp, systemui_renderer});
    params.push_back({qemuRenderengineProp, systemui_renderer});

    params.push_back({androidbootLogcatProp,
                      opts.logcat ? absl::StrReplaceAll(opts.logcat, {{" ", ","}}) : "*:V"});

    // Send adb public key to device
    auto privkey = ::goldfish::adb::GetPrivateAdbKeyPath(paths.user_directory);
    std::string key;

    if (!privkey.empty() && ::goldfish::adb::PubkeyFromPrivkey(privkey, &key)) {
        params.push_back({adbKeyProp, key});
    } else {
        LOG(WARNING) << "No adb private key exists";
    }

    // if (opts->bootchart) {
    //   params.push_back({"androidboot.bootchart", opts->bootchart});
    // }

    if (opts.selinux) {
        // TODO Must be "permissive"
        params.push_back({"androidboot.selinux", "permissive"});
    }

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

    // Keyboard config.
    params.push_back({"androidboot.qemu.keyboard_device", "QEMU Virtio Keyboard"});

    // Radio config
    params.push_back({qemuRadioDataInterfaceName, "eth0"});

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

    // Hardware decoder: disable for now
    // TODO: make it work b/449741788
    params.push_back({qemuHwcodecAvcdecProp, "0"});
    params.push_back({qemuHwcodecHevcdecProp, "0"});
    params.push_back({qemuHwcodecVpxdecProp, "0"});

    if (hasShellConsole) {
        params.push_back({"androidboot.console", absl::StrFormat("%s0", kernelSerialPrefix)});
    }

    params.push_back({avdNameProp, hw.avd_name});

    std::string deviceState = getDeviceStateString(hw);
    if (!deviceState.empty()) {
        LOG(INFO) << " sending device_state_config:" << deviceState;
        params.push_back({deviceStateProp, deviceState});
    }

    if (hw.hw_sensor_hinge) {
        int width{0}, height{0};
        width = hw.hw_displayRegion_0_1_width;
        height = hw.hw_displayRegion_0_1_height;
        std::string display_list =
                absl::StrFormat("1,%d,%d,%d,0", width, height, hw.hw_lcd_density);
        LOG(INFO) << "sending guest external displays: " << display_list;
        params.push_back({qemuExternalDisplays, display_list});
    }

    //   if (resizableEnabled()) {
    //     params.push_back({qemuDisplaySettingsXmlProp, "resizable"});
    //   }

    //   if (android_foldable_hinge_configured()) {
    //     params.push_back({autoRotateProp, "1"});
    //   }

    for (auto i = opts.append_userspace_opt; i; i = i->next) {
        const char* const val = i->param;
        if (const char* const eq = strchr(val, '=')) {
            params.push_back({std::string(val, eq), eq + 1});
        } else {
            params.push_back({val, ""});
        }
    }

    if (hw.hw_lcd_circular) {
        params.push_back({emulatorCircularProp, "1"});
    }

    if (!hw.hw_resizable_configs.empty()) {
        const auto resizable_configs =
                ::goldfish::sensors::FoldableModel::ParseResizableConfigs(hw.hw_resizable_configs);

        if (!resizable_configs) {
            LOG(ERROR) << "Failed to parse hw_resizable_configs; display configs will be skipped. "
                          "config='"
                       << hw.hw_resizable_configs << "'";
        } else if (!resizable_configs->empty()) {
            std::vector<std::string> display_configs;
            for (const auto& rc : *resizable_configs) {
                display_configs.push_back(absl::StrFormat("%d:%d:%d:%d:%d", rc.id, rc.width,
                                                          rc.height, rc.dpi, rc.dpi));
            }

            params.push_back({qemuDisplayConfigs0, absl::StrJoin(display_configs, ";")});
        }
    }

    return params;
}

std::string getDynamicPartitionBootDevice(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    auto arch = avd.Arch();
    // auto drive = emulator.get<PciDevice>("system");

    if (arch == Avd::CpuArchitecture::kX86) {
        return "pci0000:00/0000:00:03.0";
    }

    DCHECK(arch == Avd::CpuArchitecture::kArm);

    // TODO(jansene): We need should determine device id from the order they were
    // added to emulator.
    // "a003e00", "a003c00", "a003a00", "a003800", "a003600", "a003400",
    // system is currently first - "a0003e00".
    // 3c must be encrypt (metadata)
    return "a003e00.virtio_mmio";
}

std::vector<std::string> getVerifiedBootparams(const EmulatorConfig& emulator) {
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

    // if (emulator.opts().writable_system) {
    verified_boot_params.push_back("androidboot.verifiedbootstate=orange");
    // }
    return verified_boot_params;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> getBootProperties(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    auto hw = avd.Hw();

    int gles_major_version = 3;
    int gles_minor_version = 2;
    int bootPropOpenglesVersion = gles_major_version << 16 | gles_minor_version;
    std::string real_console_tty_prefix = "hvc";
    int api_level = 202504;
    auto verifiedBootParameters = getVerifiedBootparams(emulator);
    return getUserspaceBootProperties(hw.hw_cpu_arch, avd.Name(), bootPropOpenglesVersion,
                                      api_level, real_console_tty_prefix, verifiedBootParameters,
                                      avd, emulator.opts(), emulator.user_paths());
}

absl::Status InitrdDevice::initialize(const EmulatorConfig& emulator) {
    auto properties = getBootProperties(emulator);

    const Avd& avd = emulator.avd();

    fs::path system_ramdisk = avd.GetSystemImagePaths().ramdisk_image;

    // Why doesn't it use Avd::getImageFilename(Avd::ImageType::USERRAMDISK) ?
    mUserRamdisk = avd.GetContentPath() / "initrd";
    // TODO(whollins): if mUserRamdisk exists then don't overwrite it (but then boot properties
    // aren't updated)?

    // Ok.. let's create it
    LOG(INFO) << "Creating initrd from " << system_ramdisk << " -> " << mUserRamdisk;
    return ::goldfish::bootconfig::createRamdiskWithBootconfig(system_ramdisk, mUserRamdisk,
                                                               properties);
}

// TODO(jansene) add Initrd versioning magic to add/subtract parameters,
std::vector<std::string> InitrdDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-initrd", mUserRamdisk.string()};
}

}  // namespace android::goldfish

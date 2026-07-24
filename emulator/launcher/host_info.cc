#include "host_info.h"

#include "absl/log/log.h"

#include "android/base/system.h"
#include "android/cpu/cpu_accelerator.h"
#include "android/cpu/cpu_brand.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/file/file.h"

namespace android::goldfish {

namespace {

android_studio::EmulatorAvdInfo::EmulatorDeviceName getDeviceName(
        const std::string& hw_device_name) {
    using namespace android_studio;
    static const auto device_name_map =
            absl::flat_hash_map<std::string_view, EmulatorAvdInfo::EmulatorDeviceName>({
                {"resizable", EmulatorAvdInfo::RESIZABLE},
                {"7.6in Foldable", EmulatorAvdInfo::FOLDABLE_7_6_IN},
                {"small_phone", EmulatorAvdInfo::SMALL_PHONE},
                {"medium_phone", EmulatorAvdInfo::MEDIUM_PHONE},
                {"medium_tablet", EmulatorAvdInfo::MEDIUM_TABLET},
                {"pixel_c", EmulatorAvdInfo::PIXEL_C},
                {"pixel", EmulatorAvdInfo::PIXEL},
                {"pixel_xl", EmulatorAvdInfo::PIXEL_XL},
                {"pixel_2", EmulatorAvdInfo::PIXEL_2},
                {"pixel_2_xl", EmulatorAvdInfo::PIXEL_2_XL},
                {"pixel_3", EmulatorAvdInfo::PIXEL_3},
                {"pixel_3_xl", EmulatorAvdInfo::PIXEL_3_XL},
                {"pixel_3a", EmulatorAvdInfo::PIXEL_3A},
                {"pixel_3a_xl", EmulatorAvdInfo::PIXEL_3A_XL},
                {"pixel_4", EmulatorAvdInfo::PIXEL_4},
                {"pixel_4_xl", EmulatorAvdInfo::PIXEL_4_XL},
                {"pixel_4a", EmulatorAvdInfo::PIXEL_4A},
                {"pixel_5", EmulatorAvdInfo::PIXEL_5},
                {"pixel_6", EmulatorAvdInfo::PIXEL_6},
                {"pixel_6_pro", EmulatorAvdInfo::PIXEL_6_PRO},
                {"pixel_6a", EmulatorAvdInfo::PIXEL_6A},
                {"pixel_7_pro", EmulatorAvdInfo::PIXEL_7_PRO},
                {"pixel_7", EmulatorAvdInfo::PIXEL_7},
                {"pixel_7a", EmulatorAvdInfo::PIXEL_7A},
                {"pixel_8_pro", EmulatorAvdInfo::PIXEL_8_PRO},
                {"pixel_8", EmulatorAvdInfo::PIXEL_8},
                {"pixel_8a", EmulatorAvdInfo::PIXEL_8A},
                {"pixel_9", EmulatorAvdInfo::PIXEL_9},
                {"pixel_9a", EmulatorAvdInfo::PIXEL_9A},
                {"pixel_9_pro", EmulatorAvdInfo::PIXEL_9_PRO},
                {"pixel_9_pro_xl", EmulatorAvdInfo::PIXEL_9_PRO_XL},
                {"pixel_9_pro_fold", EmulatorAvdInfo::PIXEL_9_PRO_FOLD},
                {"pixel_10", EmulatorAvdInfo::PIXEL_10},
                {"pixel_10_pro", EmulatorAvdInfo::PIXEL_10_PRO},
                {"pixel_10_pro_xl", EmulatorAvdInfo::PIXEL_10_PRO_XL},
                {"pixel_10_pro_fold", EmulatorAvdInfo::PIXEL_10_PRO_FOLD},
                {"pixel_fold", EmulatorAvdInfo::PIXEL_FOLD},
                {"pixel_tablet", EmulatorAvdInfo::PIXEL_TABLET},
                {"automotive_1024p_landscape", EmulatorAvdInfo::AUTOMOTIVE_1024P_LANDSCAPE},
                {"automotive_1080p_landscape", EmulatorAvdInfo::AUTOMOTIVE_1080P_LANDSCAPE},
                {"automotive_1408p_landscape_with_play",
                 EmulatorAvdInfo::AUTOMOTIVE_1408P_LANDSCAPE_WITH_PLAY},
                {"automotive_1408p_landscape_with_google_apis",
                 EmulatorAvdInfo::AUTOMOTIVE_1408P_LANDSCAPE_WITH_GOOGLE_APIS},
                {"automotive_portrait", EmulatorAvdInfo::AUTOMOTIVE_PORTRAIT},
                {"automotive_distant_display", EmulatorAvdInfo::AUTOMOTIVE_DISTANT_DISPLAY},
                {"automotive_distant_display_with_play",
                 EmulatorAvdInfo::AUTOMOTIVE_DISTANT_DISPLAY_WITH_PLAY},
                {"automotive_ultrawide", EmulatorAvdInfo::AUTOMOTIVE_ULTRAWIDE_DISPLAY},
                {"automotive_large_portrait", EmulatorAvdInfo::AUTOMOTIVE_LARGE_PORTRAIT},
                {"desktop_small", EmulatorAvdInfo::DESKTOP_SMALL},
                {"desktop_medium", EmulatorAvdInfo::DESKTOP_MEDIUM},
                {"desktop_large", EmulatorAvdInfo::DESKTOP_LARGE},
                {"tv_4k", EmulatorAvdInfo::TV_4K},
                {"tv_1080p", EmulatorAvdInfo::TV_1080P},
                {"tv_720p", EmulatorAvdInfo::TV_720P},
                {"wearos_large_round", EmulatorAvdInfo::WEAROS_LARGE_ROUND},
                {"wearos_small_round", EmulatorAvdInfo::WEAROS_SMALL_ROUND},
                {"wearos_rect", EmulatorAvdInfo::WEAROS_RECT},
                {"wearos_square", EmulatorAvdInfo::WEAROS_SQUARE},
                {"xr_headset_device", EmulatorAvdInfo::XR_HEADSET_DEVICE},
                {"xr_glasses_device", EmulatorAvdInfo::XR_GLASSES_DEVICE},
                {"ai_glasses_device", EmulatorAvdInfo::AI_GLASSES_DEVICE},
            });
    if (auto i = device_name_map.find(hw_device_name); i != device_name_map.end()) {
        return i->second;
    }
    return android_studio::EmulatorAvdInfo::UNKNOWN_EMULATOR_DEVICE_NAME;
}

android_studio::EmulatorDetails::GuestCpuArchitecture toClearcutLogGuestArch(
        const std::string& hw_cpu_arch) {
    using namespace std::literals;
    using android_studio::EmulatorDetails;
    constexpr auto map = std::array{
        std::pair{"x86"sv, EmulatorDetails::X86},   std::pair{"x86_64"sv, EmulatorDetails::X86_64},
        std::pair{"arm"sv, EmulatorDetails::ARM},   std::pair{"arm64"sv, EmulatorDetails::ARM_64},
        std::pair{"mips"sv, EmulatorDetails::MIPS}, std::pair{"mips64"sv, EmulatorDetails::MIPS_64},
    };

    for (const auto& [name, enu] : map) {
        if (name == hw_cpu_arch) {
            return enu;
        }
    }
    return EmulatorDetails::UNKNOWN_GUEST_CPU_ARCHITECTURE;
}

android_studio::EmulatorAvdInfo::EmulatorAvdProperty toClearcutLogAvdProperty(DeviceType flavor) {
    switch (flavor) {
        using enum DeviceType;
    case kPhone:
        return android_studio::EmulatorAvdInfo::PHONE_AVD;
    case kDesktop:
        return android_studio::EmulatorAvdInfo::DESKTOP_AVD;
    case kTv:
        return android_studio::EmulatorAvdInfo::TV_AVD;
    case kWear:
        return android_studio::EmulatorAvdInfo::WEAR_AVD;
    case kAndroidAuto:
        return android_studio::EmulatorAvdInfo::ANDROIDAUTO_AVD;
    case kXr:
        return android_studio::EmulatorAvdInfo::XR_AVD;
    case kGlasses:
        return android_studio::EmulatorAvdInfo::XR_GLASSES_AVD;
    case kUnknown:
        return android_studio::EmulatorAvdInfo::UNKNOWN_EMULATOR_AVD_FLAG;
    }
    return android_studio::EmulatorAvdInfo::UNKNOWN_EMULATOR_AVD_FLAG;
}

void FillAvdFileInfo(android_studio::EmulatorAvdFile& avd_file,
                     android_studio::EmulatorAvdFile::EmulatorAvdFileKind kind,
                     const fs::path& path, bool is_custom) {
    avd_file.set_kind(kind);
    // TODO avd_file.set_size(size);
    // TODO avd_file.set_creation_timestamp(*creationTime / 1000000);
    avd_file.set_location(is_custom ? android_studio::EmulatorAvdFile::CUSTOM
                                    : android_studio::EmulatorAvdFile::STANDARD);
}

void FillAvdInfo(android_studio::EmulatorAvdInfo& avd_info, const Avd& avd) {
    const auto& hw = avd.Hw();

    // AVD name is a user-generated data, so won't report it.
    avd_info.set_api_level(avd.ApiLevel());

    avd_info.set_image_kind(avd.ImageKind());

    avd_info.set_arch(toClearcutLogGuestArch(hw.hw_cpu_arch));

    // TODO if (inAndroidBuild()) return;
    // no real AVD, so no creation times or file infos.

    /* TODO if (auto creationTime = getAvdCreationTimeSec(
                getConsoleAgents()->settings->avdInfo())) {
        avd_info.set_creation_timestamp(*creationTime);
        VERBOSE_PRINT(metrics, "AVD creation timestamp %ld", *creationTime);
    }*/

    if (auto t = avd.BuildTimestamp(); t > 0) {
        avd_info.set_build_timestamp(t);
    }
    avd_info.set_build_id(avd.BuildFingerprint());

    avd_info.set_device_name(getDeviceName(hw.hw_device_name));

    FillAvdFileInfo(*avd_info.add_files(), android_studio::EmulatorAvdFile::KERNEL,
                    /*path=*/hw.kernel_path, /*is_custom=TODO*/ false);
    FillAvdFileInfo(*avd_info.add_files(), android_studio::EmulatorAvdFile::RAMDISK,
                    /*path=*/hw.disk_ramdisk_path, /*is_custom=TODO*/ false);
    FillAvdFileInfo(*avd_info.add_files(), android_studio::EmulatorAvdFile::SYSTEM,
                    /*path=*/hw.disk_systemPartition_path, /*is_custom=TODO*/ false);

    avd_info.add_properties(toClearcutLogAvdProperty(avd.GetDeviceType()));
}

void FillHost(android_studio::EmulatorHost& host) {
    host.set_os_bit_count(64);
    const android::AndroidCpuInfoFlags cpuFlags = android::GetCpuInfo().first;
    host.set_virt_support(cpuFlags & ANDROID_CPU_INFO_VIRT_SUPPORTED);
    host.set_running_in_vm(cpuFlags & ANDROID_CPU_INFO_VM);
    host.set_cpu_manufacturer((cpuFlags & ANDROID_CPU_INFO_INTEL) ? "INTEL"
                              : (cpuFlags & ANDROID_CPU_INFO_AMD) ? "AMD"
                                                                  : "OTHER");

#if defined(__x86_64__)
    auto x86_cpuid = android::GetX86Cpuid();
    host.set_cpuid_stepping(x86_cpuid.cpuid_stepping);
    host.set_cpuid_model(x86_cpuid.cpuid_model);
    host.set_cpuid_family(x86_cpuid.cpuid_family);
    host.set_cpuid_type(x86_cpuid.cpuid_type);
    host.set_cpuid_extmodel(x86_cpuid.cpuid_extmodel);
    host.set_cpuid_extfamily(x86_cpuid.cpuid_extfamily);

    host.set_cpu_architecture("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    host.set_cpu_architecture("arm64");
#endif

    // x86_64 CPU brand name returned by CPUID can at most have 48 bytes
    // including the NULL terminator. But I don't see a definition on the
    // ARM64 side. To get it safer, use a larger buffer to hold the name.
    // So far, I have not seen a CPU whose name is 100 bytes long.
    // TODO(b/496311138): Update the implementations to check that the buffer is big enough.
    char cpuBrandName[100];
    if (!android::cpu::GetCpuBrandName(cpuBrandName)) {
        host.set_cpu_brandname(cpuBrandName);
    }
}

void FillFeatureFlagState(android_studio::EmulatorFeatureFlagState& feature_flag_state) {
    // TODO
}

void FillDetails(android_studio::EmulatorDetails& details, const Avd& avd, long launcher_pid,
                 long qemu_pid, bool metrics_collection_opt, bool fuchsia_opt, bool openglAlive) {
    FillAvdInfo(*details.mutable_avd_info(), avd);
    FillFeatureFlagState(*details.mutable_feature_flag_state());

    const auto& hw = avd.Hw();

    details.set_session_phase(android_studio::EmulatorDetails::RUNNING_GENERAL);
    details.set_is_opengl_alive(openglAlive);
    details.set_guest_arch(toClearcutLogGuestArch(hw.hw_cpu_arch));
    details.set_guest_api_level(avd.ApiLevel());

    // TODO: Check that CpuAccelerator enum +1 is the same
    // as the proto enum.
    details.set_hypervisor(static_cast<android_studio::EmulatorDetails::EmulatorHypervisor>(
            android::GetCurrentCpuAccelerator() + 1));

    details.set_guest_gpu_enabled(hw.hw_gpu_enabled);
    // TODO Set renderer details.
    /* details.set_renderer()
    if (hw.hw_gpu_enabled) {
        fillGuestGlMetrics(event);
        if (openglAlive) {
                renderer->fillGLESUsages(details.mutable_gles_usages());
        }
    }*/

    // TODO Set available GPU info.
    /*for (const GpuInfo& gpu : globalGpuInfoList().infos) {
        auto hostGpu = details.add_host_gpu();
        hostGpu->set_device_id(gpu.device_id);
        hostGpu->set_make(gpu.make);
        hostGpu->set_model(gpu.model);
        hostGpu->set_renderer(gpu.renderer);
        hostGpu->set_revision_id(gpu.revision_id);
        hostGpu->set_version(gpu.version);
    }*/

    // Check for a set of files that exist in the container environment
    bool isContainer = android::base::file::exists("/android/sdk/launch-emulator.sh") &&
                       android::base::file::exists("/tmp/pulseverbose.log");

    if (isContainer && metrics_collection_opt) {
        details.mutable_used_features()->set_launch_type(
                android_studio::EmulatorFeatures::CONTAINER);
    }

    if (fuchsia_opt) {
        details.mutable_used_features()->set_launch_type(android_studio::EmulatorFeatures::FUCHSIA);
    }

    details.set_emu_pid(launcher_pid);
    details.set_qemu_pid(qemu_pid);
    details.set_has_elevated_privileges(
            android::base::System::CurrentProcessHasElevatedPrivileges());
}
}  // namespace

void FillEmulatorHostEvent(android_studio::AndroidStudioEvent& event, const Avd& avd,
                           long launcher_pid, long qemu_pid, bool metrics_collection_opt,
                           bool fuchsia_opt) {
    event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_HOST);

    auto& product = *event.mutable_product_details();
    // TODO set product channel
    product.set_channel(android_studio::ProductDetails::UNKNOWN_LIFE_CYCLE_CHANNEL);

#ifdef __aarch64__
    product.set_os_architecture(android_studio::ProductDetails::ARM);
#else
    product.set_os_architecture(android_studio::ProductDetails::X86_64);
#endif

    FillHost(*event.mutable_emulator_host());
    FillDetails(*event.mutable_emulator_details(), avd, launcher_pid, qemu_pid,
                metrics_collection_opt, fuchsia_opt, false);
}

}  // namespace android::goldfish
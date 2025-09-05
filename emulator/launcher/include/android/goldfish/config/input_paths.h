#pragma once

#include <filesystem>

namespace fs = std::filesystem;

// These paths represent read-only inputs providing configuration and data to the emulator
// All paths should be canonicalised so as not to use
struct ResolvedInputPaths {
    fs::path launcher_binary;

    fs::path launcher_directory;
    fs::path binary_directory;
    fs::path library_directory;
    fs::path bios_directory;

    fs::path sdk_directory;
    fs::path avd_directory;
    fs::path discovery_directory;

    fs::path qemu_system_x86_binary;
    fs::path qemu_system_arm_binary;
    fs::path qemu_system_riscv_binary;
    fs::path qemu_img_binary;
    fs::path netsim_binary;
    fs::path crashpad_handler_binary;
};

// TODO
/*struct ResolvedAvdPaths {
    fs::path avd_config_ini;

    fs::path kernel_image;
    fs::path ramdisk_image;

    fs::path system_image;
    fs::path vendor_image;
    fs::path encryption_key_image;
    fs::path data_image;
    fs::path cache_image;
    fs::path sdcard_image;
};*/
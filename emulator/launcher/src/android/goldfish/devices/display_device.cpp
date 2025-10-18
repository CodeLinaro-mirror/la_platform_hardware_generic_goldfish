#include "display_device.h"

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/cmdline-definitions.h"

namespace android::goldfish {

absl::Status DisplayDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> DisplayDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    bool enable_vnc = false;
#if defined(__linux__) || defined(__APPLE__)
    enable_vnc = emulator.opts().enable_vnc;
#endif

    std::vector<std::string> params;
    params.push_back("-display");
    if (enable_vnc) {
        // This ensures that only users on local box with read/write access to that path can access
        // the VNC server. Ports can be forwarded with ssh.
        params.push_back(absl::StrCat("vnc=unix:/tmp/.qemu-emu-vnc,display=", mGpuName, ",head=0"));
        LOG(INFO) << "VNC will be available on /tmp/.qemu-emu-vnc";
        LOG(INFO) << "Tunnel over ssh with: `ssh -L localhost:5901:/tmp/.qemu-emu-vnc "
                          "<remote-host>``";
        LOG(INFO) << "Or run `socat TCP-LISTEN:5901,fork,reuseaddr "
                          "UNIX-CONNECT:/tmp/.qemu-emu-vnc` for buggy vnc viewers.";
    } else {
        params.push_back("android");
    }

    // Keyboard
    params.push_back("-device");
    params.push_back(absl::StrCat("virtio-keyboard-pci,display=", mGpuName, ",head=0"));

    // Add our virtio devices, we connect them in QEMU to gpu0 and head=%d so qemu knows how to
    // route input events for a given display to the proper device.
    constexpr int VIRTIO_INPUT_MAX_NUM = 11;
    for (int id = 0; id < VIRTIO_INPUT_MAX_NUM; id++) {
        params.push_back("-device");
        params.push_back(absl::StrCat("virtio-input-android-pci,display=", mGpuName, ",head=", id));
    }

    return params;
}

}  // namespace android::goldfish

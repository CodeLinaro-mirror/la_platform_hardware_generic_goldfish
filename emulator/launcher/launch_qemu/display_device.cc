#include "display_device.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace android::goldfish {

absl::Status DisplayDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> DisplayDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    std::vector<std::string> params;
    params.push_back("-display");
    params.push_back("android");

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

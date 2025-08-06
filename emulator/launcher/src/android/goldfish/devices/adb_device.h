#pragma once

#include <vector>
#include <string>

#include "android/goldfish/devices/device.h"
#include "absl/status/status.h"

namespace android::goldfish {

class AdbDevice : public Device {
public:
    AdbDevice() : Device("adb") {}

    absl::Status initialize(const Emulator& emulator) override;
    std::vector<std::string> getQemuParameters(
            const Emulator& emulator) const override;

private:
    uint16_t mPort{};
};

}  // namespace android::goldfish
#pragma once

#include <android/goldfish/emulator_config.h>
#include <vector>
#include <string>

#include "android/goldfish/device.h"
#include "absl/status/status.h"

namespace android::goldfish {

class AdbDevice : public Device {
public:
    AdbDevice() : Device("adb") {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(
            const EmulatorConfig& emulator) const override;

private:
    uint16_t mPort{};
};

}  // namespace android::goldfish
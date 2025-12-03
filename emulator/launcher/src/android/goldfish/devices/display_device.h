#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"

#include "android/goldfish/device.h"

namespace android::goldfish {

class DisplayDevice : public Device {
  public:
    explicit DisplayDevice(std::string_view gpu_name) : Device("display"), mGpuName(gpu_name) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  private:
    std::string mGpuName;
};

}  // namespace android::goldfish

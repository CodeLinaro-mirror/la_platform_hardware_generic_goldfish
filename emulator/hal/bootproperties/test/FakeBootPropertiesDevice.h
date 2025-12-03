#pragma once

#include <memory>

#include "android/boot/BootPropertiesDevice.h"

namespace goldfish::devices::boot {

/**
 * A fake boot properties device for testing.
 */
class FakeBootPropertiesDevice : public IBootPropertiesDevice,
                                 public std::enable_shared_from_this<FakeBootPropertiesDevice> {
  public:
    void onConnect() override { VLOG(1) << "Bootproperties device has been connected"; }
    void onClose() override { VLOG(1) << "Bootproperties device has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "Bootproperties device received " << data;
    }
};

}  // namespace goldfish::devices::boot

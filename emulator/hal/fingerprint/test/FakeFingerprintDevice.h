#pragma once

#include <memory>

#include "android/fingerprint/FingerprintDevice.h"

namespace goldfish::devices::fingerprint {

/**
 * A fake fingerprint device for testing.
 */
class FakeFingerprintDevice : public IFingerprintDevice,
                              public std::enable_shared_from_this<FakeFingerprintDevice> {
  public:
    // IFingerprintDevice implementation
    void touch(int id) override {}
    void release() override {}

    void onConnect() override { VLOG(1) << "FakeFingerprintDevice device has been connected"; }
    void onClose() override { VLOG(1) << "FakeFingerprintDevice device has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "FakeFingerprintDevice device recevied " << data;
    }
};

}  // namespace goldfish::devices::fingerprint

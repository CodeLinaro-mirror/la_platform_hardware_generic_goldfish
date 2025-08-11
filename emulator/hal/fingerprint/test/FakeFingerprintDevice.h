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

    // IPlug implementation
    bool onReceive(const void* data, size_t size) override { return true; }
    SocketPtr onUnplug() override { return nullptr; }
};

}  // namespace goldfish::devices::fingerprint

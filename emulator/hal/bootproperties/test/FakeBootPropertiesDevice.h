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
    // IBootPropertiesDevice implementation
    bool isDataPartitionMounted() override { return mDataPartitionMounted; }

    void setDataPartitionMounted(bool mounted) {
        mDataPartitionMounted = mounted;
        fireEvent(BootPropertyStatus{mounted});
    }

    // IPlug implementation
    bool onReceive(const void* data, size_t size) override { return true; }
    SocketPtr onUnplug() override { return nullptr; }

  private:
    bool mDataPartitionMounted = false;
};

}  // namespace goldfish::devices::boot

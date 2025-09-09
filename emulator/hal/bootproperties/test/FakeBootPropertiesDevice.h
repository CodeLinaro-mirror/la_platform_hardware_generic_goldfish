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
  void onConnect() override { VLOG(1) << "Bootproperties device has been connected"; }
  void onClose() override { VLOG(1) << "Bootproperties device has been disconnected"; }
  void onReceive(std::string_view data) override {
    VLOG(1) << "Bootproperties device received " << data;
  }

 private:
  bool mDataPartitionMounted = false;
};

}  // namespace goldfish::devices::boot

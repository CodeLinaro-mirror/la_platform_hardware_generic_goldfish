#pragma once

#include <memory>

#include "android/gps/GpsDevice.h"

namespace goldfish::devices::gps {

/**
 * A fake GPS device for testing.
 */
class FakeGpsDevice : public IGpsDevice, public std::enable_shared_from_this<FakeGpsDevice> {
  public:
    // IGpsDevice implementation
    void setLocation(const Location& location) override {
        mLocation = location;
        fireEvent(mLocation);
    }
    Location getLocation() const override { return mLocation; }

    // IPlug implementation
    bool onReceive(const void* data, size_t size) override { return true; }
    SocketPtr onUnplug() override { return nullptr; }

  private:
    Location mLocation;
};

}  // namespace goldfish::devices::gps

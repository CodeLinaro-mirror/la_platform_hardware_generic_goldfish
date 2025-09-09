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

    void onConnect() override { VLOG(1) << "FakeGpsDevice has been connected"; }
    void onClose() override { VLOG(1) << "FakeGpsDevice has been disconnected"; }
    void onReceive(std::string_view data) override { VLOG(1) << "FakeGpsDevice received " << data; }

  private:
    Location mLocation;
};

}  // namespace goldfish::devices::gps

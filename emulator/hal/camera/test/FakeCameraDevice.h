#pragma once

#include "goldfish/devices/camera/camera_protocol_base.h"

namespace goldfish::devices::camera {

/**
 * A fake camera device for testing.
 */
class FakeCameraDevice : public CameraProtocolBase {
  public:
    using CameraProtocolBase::CameraProtocolBase;

  protected:
    bool processQuery(std::string_view query, std::string_view params) override { return true; }
};

}  // namespace goldfish::devices::camera

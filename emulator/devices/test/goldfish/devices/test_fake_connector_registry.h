#pragma once

#include "FakeBootPropertiesDevice.h"
#include "FakeCameraDevice.h"
#include "FakeClipboardDevice.h"
#include "FakeFingerprintDevice.h"
#include "FakeGpsDevice.h"
#include "FakeGuestDevice.h"
#include "FakeSensorDevice.h"
#include "android/boot/BootPropertiesDevice.h"
#include "android/camera/CameraProtocolBase.h"
#include "android/clipboard/ClipboardDevice.h"
#include "android/fingerprint/FingerprintDevice.h"
#include "android/gps/GpsDevice.h"
#include "android/misc/GuestStatusDevice.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/sensor/SensorDevice.h"

namespace goldfish::devices {

/**
 * A fake connector registry for testing.
 */
class FakeConnectorRegistry : public ConnectorRegistry {
  public:
    FakeConnectorRegistry() {
        mBootPropertiesDevice = std::make_shared<boot::FakeBootPropertiesDevice>();
        mCameraDevice = std::make_shared<camera::FakeCameraDevice>(nullptr);
        mClipboardDevice = std::make_shared<clipboard::FakeClipboardDevice>();
        mFingerprintDevice = std::make_shared<fingerprint::FakeFingerprintDevice>();
        mGpsDevice = std::make_shared<gps::FakeGpsDevice>();
        mGuestStatusDevice = std::make_shared<guest_status::FakeGuestDevice>();
        mSensorDevice = std::make_shared<sensor::FakeSensorDevice>();

        registerTest(boot::IBootPropertiesDevice::serviceName, mBootPropertiesDevice);
        registerTest(camera::CameraDevice, mCameraDevice);
        registerTest(clipboard::IClipboardDevice::serviceName, mClipboardDevice);
        registerTest(fingerprint::IFingerprintDevice::serviceName, mFingerprintDevice);
        registerTest(gps::IGpsDevice::serviceName, mGpsDevice);
        registerTest(guest_status::IGuestStatusDevice::serviceName, mGuestStatusDevice);
        registerTest(sensor::ISensorDevice::serviceName, mSensorDevice);
    }
    ~FakeConnectorRegistry() = default;

    std::shared_ptr<boot::FakeBootPropertiesDevice> bootPropertiesDevice() {
        return mBootPropertiesDevice;
    }
    std::shared_ptr<camera::FakeCameraDevice> cameraDevice() { return mCameraDevice; }
    std::shared_ptr<clipboard::FakeClipboardDevice> clipboardDevice() { return mClipboardDevice; }
    std::shared_ptr<fingerprint::FakeFingerprintDevice> fingerprintDevice() {
        return mFingerprintDevice;
    }
    std::shared_ptr<gps::FakeGpsDevice> gpsDevice() { return mGpsDevice; }
    std::shared_ptr<guest_status::FakeGuestDevice> guestDevice() { return mGuestStatusDevice; }
    std::shared_ptr<sensor::FakeSensorDevice> sensorDevice() { return mSensorDevice; }

  private:
    void registerTest(std::string_view name, cable::PlugPtr plug) {
        registerInternal(std::string(name), plug);
    }

    std::shared_ptr<boot::FakeBootPropertiesDevice> mBootPropertiesDevice;
    std::shared_ptr<camera::FakeCameraDevice> mCameraDevice;
    std::shared_ptr<clipboard::FakeClipboardDevice> mClipboardDevice;
    std::shared_ptr<fingerprint::FakeFingerprintDevice> mFingerprintDevice;
    std::shared_ptr<gps::FakeGpsDevice> mGpsDevice;
    std::shared_ptr<guest_status::FakeGuestDevice> mGuestStatusDevice;
    std::shared_ptr<sensor::FakeSensorDevice> mSensorDevice;
};

}  // namespace goldfish::devices

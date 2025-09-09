#pragma once

#include <memory>

#include "goldfish/devices/sensor/SensorDevice.h"

namespace goldfish::devices::sensor {

/**
 * A fake sensor device for testing.
 */
class FakeSensorDevice : public ISensorDevice,
                         public std::enable_shared_from_this<FakeSensorDevice> {
  public:
    // ISensorDevice implementation
    absl::StatusOr<SensorData> getSensorData(AndroidSensor sensor_id) override {
        if (mSensorData.count(sensor_id)) {
            return mSensorData[sensor_id];
        }
        return absl::NotFoundError("Sensor not found");
    }

    absl::Status overrideSensor(AndroidSensor sensor_id, const SensorData& data) override {
        mSensorData[sensor_id] = data;
        fireEvent(sensor_id);
        return absl::OkStatus();
    }

    bool isSensorEnabled(AndroidSensor sensor_id) override {
        return mEnabledSensors.count(sensor_id) > 0;
    }
    void setSensorEnabled(AndroidSensor sensor_id, bool enabled) {
        if (enabled) {
            mEnabledSensors.insert(sensor_id);
        } else {
            mEnabledSensors.erase(sensor_id);
        }
    }

    std::chrono::microseconds getSensorTimeOffset() override { return mTimeOffset; }
    void setSensorTimeOffset(std::chrono::microseconds offset) { mTimeOffset = offset; }

    std::chrono::milliseconds getSensorDelayMs() override { return mDelayMs; }
    void setSensorDelayMs(std::chrono::milliseconds delay) { mDelayMs = delay; }

    absl::StatusOr<Rotation> getDeviceRotation() override { return mRotation; }
    void setDeviceRotation(const Rotation& rotation) { mRotation = rotation; }

    // IPlug implementation

  void onConnect() override { VLOG(1) << "FakeSensorDevice device has been connected"; }
  void onClose() override { VLOG(1) << "FakeSensorDevice device has been disconnected"; }
  void onReceive(std::string_view data) override {
    VLOG(1) << "FakeSensorDevice device received " << data;
  }

  private:
    std::map<AndroidSensor, SensorData> mSensorData;
    std::set<AndroidSensor> mEnabledSensors;
    std::chrono::microseconds mTimeOffset{0};
    std::chrono::milliseconds mDelayMs{0};
    Rotation mRotation;
};

}  // namespace goldfish::devices::sensor

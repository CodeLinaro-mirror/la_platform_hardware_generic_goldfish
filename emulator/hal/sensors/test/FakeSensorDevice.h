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
        const auto i = mSensorData.find(sensor_id);
        if (i == mSensorData.end()) {
            return absl::NotFoundError("Sensor not found");
        }

        SensorData data;
        data.measurement_id = 42;
        data.value = i->second;

        return data;
    }

    absl::Status overrideSensor(AndroidSensor sensor_id, const SensorValue& val) override {
        mSensorData[sensor_id] = val;
        fireEvent(sensor_id);
        return absl::OkStatus();
    }

    absl::StatusOr<Rotation> getDeviceRotation() override { return mRotation; }
    void setDeviceRotation(const Rotation& rotation) { mRotation = rotation; }

    // IPlug implementation

    void onConnect() override { VLOG(1) << "FakeSensorDevice device has been connected"; }
    void onClose() override { VLOG(1) << "FakeSensorDevice device has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "FakeSensorDevice device received " << data;
    }

  private:
    std::map<AndroidSensor, SensorValue> mSensorData;
    std::set<AndroidSensor> mEnabledSensors;
    Rotation mRotation;
};

}  // namespace goldfish::devices::sensor

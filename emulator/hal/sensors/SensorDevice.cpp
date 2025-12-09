// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/devices/sensor/SensorDevice.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdbool>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"

#include "android/base/system/clock.h"
#include "android/goldfish/config/device_type.h"
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/qemud.h"
#include "goldfish/physics/SkinRotation.h"
#include "goldfish/sensors/PhysicalModel.h"

namespace goldfish::devices::sensor {

using goldfish::physics::SkinRotation;
using goldfish::sensors::PhysicalModel;
using goldfish::sensors::PhysicalParameter;
using goldfish::sensors::vec3;
using goldfish::sensors::vec4;

namespace {  // Anonymous namespace for internal helpers

// Sensor information
struct SensorInfo {
    const std::string_view name;
    int id{0};
};

// Serialized sensor data
struct SerializedSensor {
    size_t measurement_id{0};
    char value[127];
    uint8_t length{0};
};

// Sensor data with enabled state
struct Sensor {
    bool enabled{false};
    SerializedSensor serialized;
};

/* a helper function that replaces commas (,) with points (.).
 * Each sensor string must be processed this way before being
 * sent into the guest. This is because the system locale may
 * cause decimal values to be formatted with a comma instead of
 * a decimal point, but that would not be parsed correctly
 * within the guest.
 */
void _sanitizeSensorString(char* string, int maxlen) {
    for (int i = 0; i < maxlen && string[i] != '\0'; i++) {
        if (string[i] == ',') {
            string[i] = '.';
        }
    }
}

const char* getSensorWireName(const AndroidSensor sensor_id) {
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) \
    case AndroidSensor::x:                 \
        return w;

    switch (sensor_id) {
        GOLDFISH_SENSORS_LIST

    case AndroidSensor::MAX_SENSORS:
        break;
    }
#undef GOLDFISH_SENSOR_DEF

    LOG(FATAL) << "Unexpected sensor_id: " << static_cast<int>(sensor_id);
}

SerializedSensor serializeSensorData(const AndroidSensor sensor_id, const SensorData& d) {
    const char* name = getSensorWireName(sensor_id);

    SerializedSensor serialized;

    switch (d.value.size()) {
    case 1:
        serialized.length = ::snprintf(serialized.value, sizeof(serialized.value), "%s:%g:%zu",
                                       name, d.value[0], d.measurement_id);
        _sanitizeSensorString(serialized.value, serialized.length);
        return serialized;

    case 3:
        serialized.length =
                ::snprintf(serialized.value, sizeof(serialized.value), "%s:%g:%g:%g:%zu", name,
                           d.value[0], d.value[1], d.value[2], d.measurement_id);
        _sanitizeSensorString(serialized.value, serialized.length);
        return serialized;

    case 4:
        serialized.length =
                ::snprintf(serialized.value, sizeof(serialized.value), "%s:%g:%g:%g:%g:%zu", name,
                           d.value[0], d.value[1], d.value[2], d.value[3], d.measurement_id);
        _sanitizeSensorString(serialized.value, serialized.length);
        return serialized;

    default:
        LOG(FATAL) << "Unexpected SensorValue size: " << d.value.size();
    }
}

}  // anonymous namespace

class SensorDevice : public ISensorDevice {
  public:
    SensorDevice(android::goldfish::DeviceType avd_type, int avd_api,
                 const android::goldfish::HardwareConfig& hw, EventLoop* eventLoop,
                 ::android::base::IClock* clock)
            : mPhysicalModel(std::make_unique<PhysicalModel>(hw))
            , mLoop(eventLoop)
            , mClock(clock)
            , mQemudParser([this](const void* data, size_t size) {
                return handleMessage(std::string_view(static_cast<const char*>(data), size));
            }) {
        // Initialize sensors based on AVD configuration
        if (hw.hw_accelerometer) {
            mSensors[static_cast<size_t>(AndroidSensor::ACCELERATION)].enabled = true;
        }
        if (hw.hw_accelerometer_uncalibrated) {
            mSensors[static_cast<size_t>(AndroidSensor::ACCELERATION_UNCALIBRATED)].enabled = true;
        }
        if (hw.hw_gyroscope) {
            mSensors[static_cast<size_t>(AndroidSensor::GYROSCOPE)].enabled = true;
        }
        if (hw.hw_sensors_proximity) {
            mSensors[static_cast<size_t>(AndroidSensor::PROXIMITY)].enabled = true;
        }
        if (hw.hw_sensors_magnetic_field) {
            mSensors[static_cast<size_t>(AndroidSensor::MAGNETIC_FIELD)].enabled = true;
        }
        if (hw.hw_sensors_magnetic_field_uncalibrated) {
            mSensors[static_cast<size_t>(AndroidSensor::MAGNETIC_FIELD_UNCALIBRATED)].enabled =
                    true;
        }
        if (hw.hw_sensors_gyroscope_uncalibrated) {
            mSensors[static_cast<size_t>(AndroidSensor::GYROSCOPE_UNCALIBRATED)].enabled = true;
        }
        if (hw.hw_sensors_orientation) {
            mSensors[static_cast<size_t>(AndroidSensor::ORIENTATION)].enabled = true;
        }
        if (hw.hw_sensors_temperature) {
            mSensors[static_cast<size_t>(AndroidSensor::TEMPERATURE)].enabled = true;
        }
        if (hw.hw_sensors_light) {
            mSensors[static_cast<size_t>(AndroidSensor::LIGHT)].enabled = true;
        }
        if (hw.hw_sensors_pressure) {
            mSensors[static_cast<size_t>(AndroidSensor::PRESSURE)].enabled = true;
        }
        if (hw.hw_sensors_humidity) {
            mSensors[static_cast<size_t>(AndroidSensor::HUMIDITY)].enabled = true;
        }
        if (hw.hw_sensors_rgbclight) {
            mSensors[static_cast<size_t>(AndroidSensor::RGBC_LIGHT)].enabled = true;
        }
        if (hw.hw_sensor_hinge) {
            mSensors[static_cast<size_t>(AndroidSensor::HINGE_ANGLE0)].enabled = true;
            switch (hw.hw_sensor_hinge_count) {
            case 3:
                mSensors[static_cast<size_t>(AndroidSensor::HINGE_ANGLE2)].enabled = true;
            case 2:
                mSensors[static_cast<size_t>(AndroidSensor::HINGE_ANGLE1)].enabled = true;
            default:;
            }
        }

        bool modernWearDevice = avd_type == android::goldfish::DeviceType::kWear && avd_api >= 28;

        if (hw.hw_sensors_heart_rate || modernWearDevice) {
            mSensors[static_cast<size_t>(AndroidSensor::HEART_RATE)].enabled = true;
        }

        if (hw.hw_sensors_wrist_tilt || modernWearDevice) {
            mSensors[static_cast<size_t>(AndroidSensor::WRIST_TILT)].enabled = true;
        }

        /* XXX: TODO: Add other tests when we add the corresponding
         * properties to hardware-properties.ini et al. */

        // Initialize physical parameters
        const float kPressure = 1013.25F;
        setPhysicalParameterValue(PhysicalParameter::PRESSURE, &kPressure, 1u,
                                  PhysicalInterpolation::SMOOTH);  // One "standard atmosphere"

        const float kProximity = 1.F;
        setPhysicalParameterValue(PhysicalParameter::PROXIMITY, &kProximity, 1u,
                                  PhysicalInterpolation::STEP);

        mEnabledMask = 0;
        for (size_t nn = 0; nn < static_cast<size_t>(AndroidSensor::MAX_SENSORS); nn++) {
            if (mSensors[nn].enabled) {
                mEnabledMask |= (1 << nn);
            }
        }
    }

    ~SensorDevice() override {}

    void onClose() override {
        VLOG(1) << "Bye bye! Sensors shutting down";
        mTimer->cancel();

        // Make sure we don't get destroyed while a timer is active.
        // By posting with a self reference we guarantee that we remain alive
        // until the timer has completed been cleaned up (b/443556478)
        (void)mLoop->post([this] { mSelf.reset(); });
    }

    void onConnect() override {
        VLOG(1) << "Starting sensor ticks" << *this;
        this->mSelf = shared_from_this();
        // Note, the timer will be scheduled after the guest requests it.
        mTimer = mLoop->createTimer([this] { tick(); });
    };

    void send(std::string_view msg) {
        auto encoded = qemud::encodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        socket()->send(encoded);
    }

    /*
     * - when the qemu-specific sensors HAL module starts, it sends
     *   "list-sensors"
     *
     * - this code replies with a string containing an integer corresponding
     *   to a bitmap of available hardware sensors in the current AVD
     *   configuration (e.g. "1" a.k.a. (1 << ANDROID_SENSOR_ACCELERATION))
     *
     * - the HAL module sends "set:<sensor>:<flag>" to enable or disable
     *   the report of a given sensor state. <sensor> must be the name of
     *   a given sensor (e.g. "accelerometer"), and <flag> must be either
     *   "1" (to enable) or "0" (to disable).
     *
     * - Once at least one sensor is "enabled", this code should periodically
     *   send information about the corresponding enabled sensors. The default
     *   period is 200ms.
     *
     * - the HAL module sends "set-delay:<delay>", where <delay> is an integer
     *   corresponding to a time delay in milli-seconds. This corresponds to
     *   a new interval between sensor events sent by this code to the HAL
     *   module.
     *
     * - the HAL module can also send a "wake" command. This code should simply
     *   send the "wake" back to the module. This is used internally to wake a
     *   blocking read that happens in a different thread. This ping-pong makes
     *   the code in the HAL module very simple.
     *
     * - each timer tick, this code sends sensor reports in the following
     *   format (each line corresponds to a different line sent to the module):
     *
     *      acceleration:<x>:<y>:<z>
     *      magnetic-field:<x>:<y>:<z>
     *      orientation:<azimuth>:<pitch>:<roll>
     *      temperature:<celsius>
     *      light:<lux>
     *      pressure:<hpa>
     *      humidity:<percent>
     *      sync:<time_us>
     *
     *   Where each line before the sync:<time_us> is optional and will only
     *   appear if the corresponding sensor has been enabled by the HAL module.
     *
     *   Note that <time_us> is the VM time in micro-seconds when the report
     *   was "taken" by this code. This is adjusted by the HAL module to
     *   emulated system time (using the first sync: to compute an adjustment
     *   offset).
     */
    void onReceive(std::string_view data) override {
        mQemudParser.onReceive(data.data(), data.size());
    }

    bool handleMessage(std::string_view msg) {
        DCHECK(mTimer)
                << "onReceive must have been called before onConnected was called, this "
                   "means we are operating on an unconnected socket, and the guest will not "
                   "receive the expected response! Logcat will likely show a crashed sensor hal.";

        VLOG(2) << "Received message from sensor HAL: " << msg;
        if (msg == "list-sensors") {
            std::string response = std::to_string(mEnabledMask);
            send(response);
            return true;
        }

        if (msg == "wake") {
            send("wake");
            return true;
        }

        if (absl::ConsumePrefix(&msg, "set-delay:")) {
            int32_t delay_ms;
            if (absl::SimpleAtoi(msg, &delay_ms)) {
                mDelay = absl::Milliseconds(delay_ms);
                if (mEnabledMask != 0) {
                    // Trigger a tick to apply the new delay immediately.
                    tick();
                }
                return true;
            } else {
                VLOG(1) << "Ignoring 'set-delay' command with invalid delay value: '" << msg << "'";
                return true;
            }
        }

        if (absl::ConsumePrefix(&msg, "set:")) {
            std::vector<std::string_view> parts = absl::StrSplit(msg, ':');
            if (parts.size() != 2) {
                VLOG(1) << "Ignoring malformed 'set' command. Expected format "
                        << "'set:<sensor>:<0|1>', but received: 'set:" << msg << "'";
                return true;
            }

            int id = sensorIdFromName(parts[0]);
            if (id < 0 || id >= static_cast<int>(AndroidSensor::MAX_SENSORS)) {
                VLOG(1) << "Ignoring 'set' command for unknown sensor: '" << parts[0] << "'";
                return true;
            }

            if (!mSensors[id].enabled) {
                VLOG(1) << "Ignoring 'set' command for sensor '" << parts[0]
                        << "' which is not enabled by AVD configuration.";
                return true;
            }

            bool enabled = (parts[1] == "1");
            if (enabled) {
                mEnabledMask |= (1 << id);
            } else {
                mEnabledMask &= ~(1 << id);
            }

            // Trigger a tick to apply the new mask configuration immediately.
            tick();
            return true;
        }

        if (absl::ConsumePrefix(&msg, "time:")) {
            int64_t guest_time_ns;
            if (absl::SimpleAtoi(msg, &guest_time_ns)) {
                auto now = mClock->now(::android::base::ClockType::Virtual);
                mTimeOffset = absl::FromUnixNanos(guest_time_ns) - now;
                return true;
            } else {
                VLOG(1) << "Ignoring 'time' command with invalid timestamp value: '" << msg << "'";
                return true;
            }
        }

        VLOG(1) << "Ignoring unknown command from sensor HAL: " << msg;
        return true;
    }

    absl::Status overrideSensor(AndroidSensor sensor_id, const SensorValue& val) override {
        if (sensor_id >= AndroidSensor::MAX_SENSORS) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "SensorId: %zu, out of range (max:%zu)", static_cast<size_t>(sensor_id),
                    static_cast<size_t>(AndroidSensor::MAX_SENSORS)));
        }

        if (!mSensors[static_cast<size_t>(sensor_id)].enabled) {
            return absl::UnavailableError("The sensor is disabled");
        }

        switch (sensor_id) {
        case AndroidSensor::HINGE_ANGLE0:
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE0, val.data(), val.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        case AndroidSensor::HINGE_ANGLE1:
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE1, val.data(), val.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        case AndroidSensor::HINGE_ANGLE2:
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE2, val.data(), val.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        default:
            setSensorValue(sensor_id, val);
            break;
        }

        fireEvent(sensor_id);
        return absl::OkStatus();
    }

    absl::StatusOr<SensorData> getSensorData(AndroidSensor sensor_id) override {
        if (sensor_id >= AndroidSensor::MAX_SENSORS) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "SensorId: %zu, out of range (max:%zu)", static_cast<size_t>(sensor_id),
                    static_cast<size_t>(AndroidSensor::MAX_SENSORS)));
        }

        if (!mSensors[static_cast<size_t>(sensor_id)].enabled) {
            return absl::UnavailableError("The sensor is disabled");
        }

        return mPhysicalModel->getSensorData(sensor_id);
    }

    absl::StatusOr<Rotation> getDeviceRotation() override {
        const auto out = getSensorData(AndroidSensor::ACCELERATION);
        if (!out.ok()) {
            return out.status();
        }
        const SensorValue& val = out->value;

        glm::vec3 device_accelerometer(val[0], val[1], val[2]);
        glm::vec3 normalized_accelerometer = glm::normalize(device_accelerometer);

        static const std::array<std::pair<glm::vec3, SkinRotation>, 4> directions{
            std::make_pair(glm::vec3(0.0f, 1.0f, 0.0f), SkinRotation::PORTRAIT),
            std::make_pair(glm::vec3(1.0f, 0.0f, 0.0f), SkinRotation::LANDSCAPE),
            std::make_pair(glm::vec3(0.0f, -1.0f, 0.0f), SkinRotation::REVERSE_PORTRAIT),
            std::make_pair(glm::vec3(-1.0f, 0.0f, 0.0f), SkinRotation::REVERSE_LANDSCAPE)};
        auto coarse_orientation = SkinRotation::PORTRAIT;
        for (const auto& v : directions) {
            if (fabs(glm::dot(normalized_accelerometer, v.first) - 1.f) < 0.1f) {
                coarse_orientation = v.second;
                break;
            }
        }

        Rotation r = {
            .rotation = coarse_orientation, .xAxis = val[0], .yAxis = val[1], .zAxis = val[2]};
        return r;
    }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[SensorDevice socket=%v]", *socket());
    }

  private:
    // Helper functions to get sensor/parameter ID from name
    int sensorIdFromName(std::string_view name) const {
        for (int i = 0; i < static_cast<int>(AndroidSensor::MAX_SENSORS); i++) {
            if (kSensors[i].name == name) {
                return i;
            }
        }
        return -1;
    }

    // Helper functions to set/get sensor values
    void setSensorValue(AndroidSensor sensor_id, const SensorValue& val) {
        mPhysicalModel->setSensorValue(sensor_id, val);
    }

    void setPhysicalParameterValue(PhysicalParameter parameter, const float* val,
                                   const size_t count, PhysicalInterpolation interpolation_mode) {
        mPhysicalModel->setPhysicalParameterValue(parameter, val, count, interpolation_mode);
    }

    bool enabled(int sensorId) { return (mEnabledMask & (1 << sensorId)) != 0; }

    void tick() {
        // Grab the guest time before sending any sensor data:
        // the android.hardware CTS requires sync times to be no greater than the
        // time of the sensor event arrival. Since the CTS enforces this property,
        // other code may also rely on it.
        DCHECK(mLoop->isOnLoopThread()) << "Tick must be called from the event loop!";
        const auto now = mClock->now(::android::base::ClockType::Virtual);
        mPhysicalModel->setCurrentTime(absl::ToUnixNanos(now));
        for (size_t sensor_id = 0; sensor_id < static_cast<size_t>(AndroidSensor::MAX_SENSORS);
             ++sensor_id) {
            if (!enabled(sensor_id)) {
                continue;
            }

            const SensorData d =
                    mPhysicalModel->getSensorData(static_cast<AndroidSensor>(sensor_id));
            Sensor& s = mSensors[sensor_id];

            s.serialized = serializeSensorData(static_cast<AndroidSensor>(sensor_id), d);
            send(std::string_view(s.serialized.value, s.serialized.length));
        }

        send(absl::StrFormat("guest-sync:%d", absl::ToUnixNanos(now + mTimeOffset)));
        send(absl::StrFormat("sync:%d", absl::ToUnixMicros(now)));

        if (mEnabledMask == 0) return;

        // Rearm the timer to fire a little bit early, so we can sustain the
        // requested frequency. Also make sure we have at least a minimal delay,
        // otherwise this timer would hijack the main loop thread and won't allow
        // guest to ever run.
        // Note: While there is some overhead in this code, it is signifcantly less
        //  than 1ms. Just delay by exactly (delay_ms) below to keep the actual rate
        //  as close to the desired rate as possible.
        // Note2: Let's cap the minimal tick interval to 10ms, to make sure:
        // - We never overload the main QEMU loop.
        // - Some CTS hardware test cases require a limit on the maximum update
        // rate,
        //   which has been known to be in the low 100's of Hz.
        mDelay = std::clamp(mDelay, absl::Milliseconds(10), absl::Hours(1));

        DCHECK(mSelf) << "Self reference should have been set, otherwise we are scheduling a "
                         "callback where we can disappear from (i.e. tick could be called with "
                         "this == nullptr)!";
        mTimer->schedule(absl::ToChronoMilliseconds(mDelay), absl::ToChronoMilliseconds(mDelay));
    }

    std::unique_ptr<PhysicalModel> mPhysicalModel;
    EventLoop* const mLoop;
    ::android::base::IClock* const mClock;
    qemud::Parser mQemudParser;
    std::shared_ptr<EventLoop::Timer> mTimer;
    absl::Duration mTimeOffset;
    absl::Duration mDelay{absl::Milliseconds(800)};

    // We are having callbacks in a timer, we want to make sure we never
    // delete ourselves.
    std::shared_ptr<ISensorDevice> mSelf;

    Sensor mSensors[static_cast<size_t>(AndroidSensor::MAX_SENSORS)];
    uint32_t mEnabledMask{0};

    // Sensor and Physical Parameter information arrays
    static constexpr SensorInfo kSensors[static_cast<size_t>(AndroidSensor::MAX_SENSORS)] = {
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) {y, static_cast<int>(AndroidSensor::x)},
        GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF
    };
};

void ISensorDevice::registerDevice(IConnectorRegistry* registry,
                                   android::goldfish::DeviceType avd_type, int avd_api,
                                   const android::goldfish::HardwareConfig& hw,
                                   EventLoop* clientLoop, EventLoop* qemuLoop,
                                   ::android::base::IClock* clock) {
    registry->registerHalQemuDevice(std::string(ISensorDevice::serviceName), clientLoop, qemuLoop,
                                    [avd_type, avd_api, &hw, clientLoop, clock]() {
                                        return std::make_shared<SensorDevice>(avd_type, avd_api, hw,
                                                                              clientLoop, clock);
                                    });
}

// Registers the sensor device with the registry
void ISensorDevice::registerDevice(IConnectorRegistry* registry,
                                   android::goldfish::DeviceType avd_type, int avd_api,
                                   const android::goldfish::HardwareConfig& hw,
                                   EventLoop* clientLoop, EventLoop* qemuLoop) {
    registerDevice(registry, avd_type, avd_api, hw, clientLoop, qemuLoop,
                   &::android::base::IClock::get());
}

}  // namespace goldfish::devices::sensor

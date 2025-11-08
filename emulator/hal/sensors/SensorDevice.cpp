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
#include "goldfish/devices/sensor/PhysicalModel.h"
#include "goldfish/devices/sensor/Sensors.h"
#include "goldfish/physics/SkinRotation.h"

namespace goldfish::devices::sensor {

using goldfish::physics::SkinRotation;

namespace {  // Anonymous namespace for internal helpers

// Sensor information
struct SensorInfo {
    const std::string_view name;
    int id{0};
};

// Physical parameter information
struct PhysicalParameterInfo {
    const std::string_view name;
    int id{0};
};

// Serialized sensor data
struct SerializedSensor {
    unsigned long measurement_id{0};
    int length{0};
    char value[128];
};

// Sensor data with enabled state
struct Sensor {
    bool enabled{false};
    SerializedSensor serialized;
};

// Helper functions to get vec3/vec4 values from float array
void getvec3Size(size_t* size) {
    if (size != nullptr) {
        *size = 3;
    }
}

void getvec4Size(size_t* size) {
    if (size != nullptr) {
        *size = 4;
    }
}

void getfloatSize(size_t* size) {
    if (size != nullptr) {
        *size = 1;
    }
}

vec3 getvec3Value(const float* val, const size_t count) {
    return vec3{count > 0 ? val[0] : 0, count > 1 ? val[1] : 0, count > 2 ? val[2] : 0};
}

vec4 getvec4Value(const float* val, const size_t count) {
    return vec4{count > 0 ? val[0] : 0, count > 1 ? val[1] : 0, count > 2 ? val[2] : 0,
                count > 3 ? val[3] : 0};
}

float getfloatValue(const float* val, const size_t count) {
    return count > 0 ? val[0] : 0;
}

// Helper functions to get size and values from vec3/vec4/float
template <typename T>
void getSize(size_t* size) {
    if (size != nullptr) {
        if constexpr (std::is_same_v<T, vec3>) {
            *size = 3;
        } else if constexpr (std::is_same_v<T, vec4>) {
            *size = 4;
        } else if constexpr (std::is_same_v<T, float>) {
            *size = 1;
        } else {
            static_assert(false, "Unsupported type for getSize");
        }
    }
}

void getValues(const vec3 value, float* const* out, const size_t count) {
    if (count > 0) *out[0] = value.x;
    if (count > 1) *out[1] = value.y;
    if (count > 2) *out[2] = value.z;
}

void getValues(const vec4 value, float* const* out, const size_t count) {
    if (count > 0) *out[0] = value.x;
    if (count > 1) *out[1] = value.y;
    if (count > 2) *out[2] = value.z;
    if (count > 3) *out[3] = value.w;
}

void getValues(const float value, float* const* out, const size_t count) {
    if (count > 0) *out[0] = value;
}

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

}  // anonymous namespace

class SensorDevice : public ISensorDevice {
  public:
    SensorDevice(android::goldfish::DeviceType avd_type, int avd_api, const android::goldfish::HardwareConfig& hw, EventLoop* eventLoop,
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
        mLoop->post([this] { mSelf.reset(); });
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

    absl::Status overrideSensor(AndroidSensor sensor_id, const SensorData& data) override {
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
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE0, data.data(), data.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        case AndroidSensor::HINGE_ANGLE1:
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE1, data.data(), data.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        case AndroidSensor::HINGE_ANGLE2:
            setPhysicalParameterValue(PhysicalParameter::HINGE_ANGLE2, data.data(), data.size(),
                                      PhysicalInterpolation::SMOOTH);

            break;
        default:
            setSensorValue(sensor_id, data.data(), data.size());
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

        size_t out;
        getSensorValueSize(sensor_id, &out);
        std::vector<float> val(out, 0);
        std::vector<float*> ptr;
        for (int i = 0; i < val.size(); i++) {
            ptr.push_back(&val[i]);
        }

        getSensorValue(sensor_id, ptr.data(), ptr.size());
        return val;
    }

    bool isSensorEnabled(AndroidSensor sensor_id) override {
        if (sensor_id >= AndroidSensor::MAX_SENSORS) return false;

        return mSensors[static_cast<size_t>(sensor_id)].enabled;
    }

    std::chrono::microseconds getSensorTimeOffset() override {
        return absl::ToChronoMicroseconds(mTimeOffset);
    }

    std::chrono::milliseconds getSensorDelayMs() override {
        return absl::ToChronoMilliseconds(mDelay);
    }

    absl::StatusOr<Rotation> getDeviceRotation() override {
        auto out = getSensorData(AndroidSensor::ACCELERATION);
        if (!out.ok()) {
            return out.status();
        }
        glm::vec3 device_accelerometer(out->at(0), out->at(1), out->at(2));
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

        Rotation r = {.rotation = coarse_orientation,
                      .xAxis = out->at(0),
                      .yAxis = out->at(1),
                      .zAxis = out->at(2)};
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

    // Helper functions to serialize sensor data
    void serializeSensorValue(Sensor& sensor, AndroidSensor sensor_id) {
        long measurement_id = -1L;

        switch (sensor_id) {
#define ENUM_NAME(x) AndroidSensor::x
#define GET_FUNCTION_NAME(x) get##x
#define SERIALIZE_VALUE_NAME(x) serializeValue
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                             \
    case ENUM_NAME(x): {                                                               \
        const v current_value = mPhysicalModel->GET_FUNCTION_NAME(z)(&measurement_id); \
        if (measurement_id != sensor.serialized.measurement_id) {                      \
            SERIALIZE_VALUE_NAME(v)                                                    \
            (sensor, w ":%ld", current_value, measurement_id);                         \
        }                                                                              \
        break;                                                                         \
    }
            GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF
#undef SERIALIZE_VALUE_NAME
#undef GET_FUNCTION_NAME
#undef ENUM_NAME
        default:
            assert(false);  // should never happen
            return;
        }
        assert(sensor.serialized.length < sizeof(sensor.serialized.value));

        if (measurement_id != sensor.serialized.measurement_id) {
            _sanitizeSensorString(sensor.serialized.value, sensor.serialized.length);
        }
        sensor.serialized.measurement_id = measurement_id;
    }

    void serializeValue(Sensor& sensor, const char* format, float value, long measurement_id) {
        sensor.serialized.length =
                snprintf(sensor.serialized.value, sizeof(sensor.serialized.value), format, value,
                         measurement_id);
    }

    void serializeValue(Sensor& sensor, const char* format, vec3 value, long measurement_id) {
        sensor.serialized.length =
                snprintf(sensor.serialized.value, sizeof(sensor.serialized.value), format, value.x,
                         value.y, value.z, measurement_id);
    }

    void serializeValue(Sensor& sensor, const char* format, vec4 value, long measurement_id) {
        sensor.serialized.length =
                snprintf(sensor.serialized.value, sizeof(sensor.serialized.value), format, value.x,
                         value.y, value.z, value.w, measurement_id);
    }

    // Helper functions to set/get sensor values
    void setSensorValue(AndroidSensor sensor_id, const float* val, const size_t count) {
        switch (sensor_id) {
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define GET_TYPE_VALUE_FUNCTION_NAME(x) get##x##Value
#define ENUM_NAME(x) AndroidSensor::x
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                                      \
    case ENUM_NAME(x):                                                                          \
        mPhysicalModel->OVERRIDE_FUNCTION_NAME(z)(GET_TYPE_VALUE_FUNCTION_NAME(v)(val, count)); \
        break;
            GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF
#undef ENUM_NAME
#undef GET_TYPE_VALUE_FUNCTION_NAME
#undef OVERRIDE_FUNCTION_NAME
        default:
            assert(false);  // should never happen
            break;
        }
    }

    void getSensorValue(AndroidSensor sensor_id, float* const* out, const size_t count) {
        long measurement_id;
        switch (sensor_id) {
#define GET_FUNCTION_NAME(x) mPhysicalModel->get##x
#define TYPE_GET_VALUES_FUNCTION_NAME(x) getValues
#define ENUM_NAME(x) AndroidSensor::x
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                   \
    case ENUM_NAME(x):                                       \
        TYPE_GET_VALUES_FUNCTION_NAME(v)                     \
        (GET_FUNCTION_NAME(z)(&measurement_id), out, count); \
        break;
            GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF
#undef ENUM_NAME
#undef TYPE_GET_VALUES_FUNCTION_NAME
#undef GET_FUNCTION_NAME
        default:
            assert(false);  // should never happen
            break;
        }
    }

    void getSensorValueSize(AndroidSensor sensor_id, size_t* size) const {
        switch (sensor_id) {
#define GET_FUNCTION_NAME(x) physicalModel_get##x
#define TYPE_GET_VALUES_FUNCTION_NAME(x) get##x##Size
#define ENUM_NAME(x) AndroidSensor::x
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) \
    case ENUM_NAME(x):                     \
        TYPE_GET_VALUES_FUNCTION_NAME(v)   \
        (size);                            \
        break;
            GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF
#undef ENUM_NAME
#undef TYPE_GET_VALUES_FUNCTION_NAME
#undef GET_FUNCTION_NAME
        default:
            assert(false);  // should never happen
            break;
        }
    }

    // Helper functions for physical parameters
    void setPhysicalParameterValue(PhysicalParameter parameter, const float* val,
                                   const size_t count, PhysicalInterpolation interpolation_mode) {
        switch (parameter) {
#define ENUM_NAME(x) PhysicalParameter::x
#define GET_TYPE_VALUE_FUNCTION_NAME(x) get##x##Value
#define SET_TARGET_FUNCTION_NAME(x) mPhysicalModel->setTarget##x
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w)                        \
    case ENUM_NAME(x):                                                     \
        SET_TARGET_FUNCTION_NAME(z)                                        \
        (GET_TYPE_VALUE_FUNCTION_NAME(w)(val, count), interpolation_mode); \
        break;
            GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF
#undef SET_TARGET_FUNCTION_NAME
#undef GET_TYPE_VALUE_FUNCTION_NAME
#undef ENUM_NAME
        default:
            assert(false);  // should never happen
            break;
        }

        // TODO(jansene): (fire sensor change event)
    }

    void getPhysicalParameterValue(PhysicalParameter parameter_id, float* const* out,
                                   const size_t count, ParameterValueType parameter_value_type) {
        switch (parameter_id) {
#define ENUM_NAME(x) PhysicalParameter::x
#define TYPE_GET_VALUES_FUNCTION_NAME(x) getValues
#define GET_PARAMETER_FUNCTION_NAME(x) mPhysicalModel->getParameter##x
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w)                         \
    case ENUM_NAME(x):                                                      \
        TYPE_GET_VALUES_FUNCTION_NAME(w)                                    \
        (GET_PARAMETER_FUNCTION_NAME(z)(parameter_value_type), out, count); \
        break;
            GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GET_PARAMETER_FUNCTION_NAME
#undef TYPE_GET_VALUES_FUNCTION_NAME
#undef ENUM_NAME
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF
        default:
            assert(false);  // should never happen
            break;
        }
    }

    void getPhysicalParameterValueSize(PhysicalParameter parameter_id, size_t* size) const {
        switch (parameter_id) {
#define ENUM_NAME(x) PhysicalParameter::x
#define TYPE_GET_VALUES_FUNCTION_NAME(x) get##x##Size
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w) \
    case ENUM_NAME(x):                              \
        TYPE_GET_VALUES_FUNCTION_NAME(w)            \
        (size);                                     \
        break;
            GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF
#undef TYPE_GET_VALUES_FUNCTION_NAME
#undef ENUM_NAME
        default:
            assert(false);  // should never happen
            break;
        }
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
            serializeSensorValue(mSensors[sensor_id], (AndroidSensor)sensor_id);
            send(std::string_view(mSensors[sensor_id].serialized.value,
                                  mSensors[sensor_id].serialized.length));
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

    Sensor mSensors[static_cast<size_t>(AndroidSensor::MAX_SENSORS)];
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    absl::Duration mTimeOffset;
    EventLoop* mLoop;
    std::shared_ptr<EventLoop::Timer> mTimer;
    ::android::base::IClock* mClock;
    uint32_t mEnabledMask{0};
    absl::Duration mDelay{absl::Milliseconds(800)};
    qemud::Parser mQemudParser;

    // We are having callbacks in a timer, we want to make sure we never
    // delete ourselves.
    std::shared_ptr<ISensorDevice> mSelf;

    // Sensor and Physical Parameter information arrays
    static constexpr SensorInfo kSensors[static_cast<size_t>(AndroidSensor::MAX_SENSORS)] = {
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) {y, static_cast<int>(AndroidSensor::x)},
        GOLDFISH_SENSORS_LIST
#undef SENSOR_
    };
};

void ISensorDevice::registerDevice(IConnectorRegistry* registry, android::goldfish::DeviceType avd_type, int avd_api, const android::goldfish::HardwareConfig& hw,
                                   EventLoop* clientLoop, EventLoop* qemuLoop,
                                   ::android::base::IClock* clock) {
    registry->registerHalQemuDevice(std::string(ISensorDevice::serviceName), clientLoop, qemuLoop,
                                    [avd_type, avd_api, &hw, clientLoop, clock]() {
                                        return std::make_shared<SensorDevice>(avd_type, avd_api, hw, clientLoop, clock);
                                    });
}

// Registers the sensor device with the registry
void ISensorDevice::registerDevice(IConnectorRegistry* registry, android::goldfish::DeviceType avd_type, int avd_api, const android::goldfish::HardwareConfig& hw,
                                   EventLoop* clientLoop, EventLoop* qemuLoop) {
    registerDevice(registry, avd_type, avd_api, hw, clientLoop, qemuLoop, &::android::base::IClock::get());
}

SensorObserver::SensorObserver(ConnectorRegistry* registry, AndroidSensor id)
        : mDeviceListener(registry), mId(id) {
    mDeviceListener.addCallback(
            [this](std::weak_ptr<ISensorDevice> device) { registerDevice(device); });
}

void SensorObserver::registerDevice(std::weak_ptr<ISensorDevice> weakSensor) {
    if (auto sensor = weakSensor.lock()) {
        mDevice = sensor;
        mCallbackId =
                sensor->addCallback([this](AndroidSensor sensorId) { forwardEvent(sensorId); });
    }
}

void SensorObserver::forwardEvent(const AndroidSensor sensorId) {
    if (sensorId != mId) {
        return;
    }
    auto device = mDevice.lock();
    if (!device) {
        return;
    }
    auto data = device->getSensorData(sensorId);
    if (!data.ok()) {
        return;
    }

    if (mOld != data.value()) {
        mOld = data.value();
        SensorObserver::fireEvent(mOld);
    }
}

SensorObserver::~SensorObserver() {
    if (auto sensor = mDevice.lock()) {
        sensor->removeCallback(mCallbackId);
    }
}

}  // namespace goldfish::devices::sensor

/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <mutex>

#include "aemu/base/EventNotificationSupport.h"
#include "aemu/base/events/EventSources.h"
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/physics/AmbientEnvironment.h"
#include "goldfish/physics/BodyModel.h"
#include "goldfish/physics/InertialModel.h"
#include "goldfish/physics/Physics.h"
#include "goldfish/sensors/AndroidSensor.h"
#include "goldfish/sensors/FoldableModel.h"
#include "goldfish/sensors/PhysicalParameter.h"
#include "goldfish/sensors/sensor_data.h"

namespace goldfish::sensors {

using android::base::eventing::CallbackEventSource;

using ::goldfish::physics::AmbientEnvironment;
using ::goldfish::physics::BodyModel;
using ::goldfish::physics::InertialModel;

using glm::vec3;
using glm::vec4;

class PhysicalModel;

/**
 * @brief Event structure for physical model state changes.
 */
struct PhysicalModelChangeEvent {
    /**
     * @brief Types of physical model state changes.
     */
    enum class Type {
        /// Notify the agent that physical target states have changed
        TargetStateChanged,
        /// Notify the agent that physical state changes are beginning
        PhysicalStateChanging,
        /// Notify the agent that physical state changes are complete and model is stable
        PhysicalStateStabilized,
    };

    Type type;             ///< Type of state change event
    PhysicalModel* model;  ///< Pointer to the model generating the event
};

/**
 * @brief Simulates an ambient environment with a rigid body and generates sensor data.
 *
 * This class provides a comprehensive physical simulation environment for Android
 * device emulation. It models various physical aspects including:
 * - Position and orientation in 3D space
 * - Ambient environmental conditions (temperature, pressure, etc.)
 * - Device sensors (accelerometer, gyroscope, etc.)
 * - Foldable device states and transformations
 *
 * The model supports both immediate and interpolated state changes, sensor value
 * overrides, and provides event notifications for state changes.
 *
 * @note All public methods are thread-safe unless otherwise specified.
 */
class PhysicalModel : public CallbackEventSource<PhysicalModelChangeEvent> {
  public:
    static constexpr size_t kNumSensors = static_cast<size_t>(AndroidSensor::MAX_SENSORS);

    PhysicalModel(const android::goldfish::HardwareConfig& hw);
    ~PhysicalModel() = default;

    SensorData getSensorData(AndroidSensor) const;
    void setSensorValue(AndroidSensor, const SensorValue&);

    void setPhysicalParameterValue(PhysicalParameter parameter, const float* val,
                                   const size_t count, PhysicalInterpolation interpolation_mode);

    /**
     * @brief Sets the current simulation time.
     *
     * This time is used for calculating sensor values and recording when target
     * parameter changes occur. Time values should be monotonic (non-decreasing).
     *
     * @param time_ns The current time in nanoseconds.
     */
    void setCurrentTime(int64_t time_ns);

    /**
     * @brief Sets the gravity vector for the simulation.
     * @param x X component of gravity vector
     * @param y Y component of gravity vector
     * @param z Z component of gravity vector
     */
    void setGravity(float x, float y, float z);

    /*
     * Target state setters and parameter getters
     */
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w) \
    void setTarget##z(w value, PhysicalInterpolation mode);

    GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF

    /*
     * Gets current target state of the modeled object.
     */
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w) \
    w getParameter##z(ParameterValueType parameterValueType) const;

    GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF

    /*
     * Sensor override methods
     */
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) void override##z(v override_value);
    GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

    /*
     * Getters for all sensor values.
     * Can be called from any thread.
     */
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) v get##z(size_t* measurement_id) const;
    GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

    /**
     * @brief Gets the current physical transform of the device.
     * @param[out] out_translation_x X translation
     * @param[out] out_translation_y Y translation
     * @param[out] out_translation_z Z translation
     * @param[out] out_rotation_x X rotation in degrees
     * @param[out] out_rotation_y Y rotation in degrees
     * @param[out] out_rotation_z Z rotation in degrees
     * @param[out] out_timestamp Timestamp in nanoseconds
     */
    void getTransform(float* out_translation_x, float* out_translation_y, float* out_translation_z,
                      float* out_rotation_x, float* out_rotation_y, float* out_rotation_z,
                      int64_t* out_timestamp) const;

    /**
     * @brief Gets the current foldable device state.
     * @return Current foldable state
     */
    FoldableState getFoldableState() const;

    /**
     * @brief Checks if the foldable device is currently folded.
     * @return true if device is folded, false otherwise
     */
    bool foldableIsFolded() const;

    /**
     * @brief Gets the folded area dimensions.
     * @param[out] x X coordinate of folded area
     * @param[out] y Y coordinate of folded area
     * @param[out] w Width of folded area
     * @param[out] h Height of folded area
     * @return true if area was retrieved successfully
     */
    bool getFoldedArea(int* x, int* y, int* w, int* h) const;

    android::base::EventNotificationSupport<FoldablePostures>* getPostureListener();

  private:
    static size_t getSensorValueSize(AndroidSensor);
    size_t getSensorDataImpl(AndroidSensor, float* out, const size_t count) const;
    void setSensorValueImpl(AndroidSensor, const float* val, const size_t count);

    /*
     * Sets the target value for the given physical parameter that the physical
     * model should move towards.
     * Can be called from any thread.
     */
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w) \
    void setTargetInternal##z(w value, PhysicalInterpolation mode);

    GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF

    /*
     * Getters for non-overridden physical sensor values.
     */
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) v getPhysical##z() const;
    GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

    /*
     * Helper for setting overrides.
     */
    template <class T>
    void setOverride(const AndroidSensor sensor, T* overrideMemberPointer, T overrideValue) {
        const size_t sensorIndex = static_cast<size_t>(sensor);

        physicalStateChanging();
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            mUseOverride[sensorIndex] = true;
            mMeasurementId[sensorIndex]++;
            *overrideMemberPointer = overrideValue;
        }
    }

    /*
     * Helper for getting current sensor values.
     */
    template <class T, class GETTER>
    T getSensorValue(const AndroidSensor sensor, const T* overrideMemberPointer,
                     const GETTER& physicalGetter, size_t* measurement_id) const;

    void physicalStateChanging();    ///< Called when physical state begins changing
    void physicalStateStabilized();  ///< Called when physical state stabilizes
    void targetStateChanged();       ///< Called when target state changes

    mutable std::recursive_mutex mMutex;  ///< Mutex for thread safety

    InertialModel mInertialModel;            ///< Models inertial motion
    AmbientEnvironment mAmbientEnvironment;  ///< Models ambient conditions
    FoldableModel mFoldableModel;            ///< Models foldable device state
    BodyModel mBodyModel;                    ///< Models body-related sensors

    mutable size_t mMeasurementId[kNumSensors] = {0};  ///< Measurement IDs

    bool mIsPhysicalStateChanging{false};      ///< True if physical state is changing
    bool isLoadingSnapshot{false};             ///< True if loading from snapshot
    bool mUseOverride[kNumSensors] = {false};  ///< Sensor override flags

#define GOLDFISH_SENSOR_DEF(x, y, z, v, w) v m##z##Override{0.f};
    GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

    int64_t mModelTimeNs = 0L;  ///< Current model time in nanoseconds
};

}  // namespace goldfish::sensors
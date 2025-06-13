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

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <mutex>

#include "aemu/base/EventNotificationSupport.h"
#include "android/emulation/control/utils/CallbackEventSupport.h"
#include "android/goldfish/config/avd.h"
#include "android/physics/Foldable.h"
#include "android/physics/FoldableModel.h"
#include "android/physics/Sensors.h"
#include "goldfish/physics/AmbientEnvironment.h"
#include "goldfish/physics/BodyModel.h"
#include "goldfish/physics/InertialModel.h"
#include "goldfish/physics/Physics.h"

namespace android {
namespace physics {

using android::emulation::control::EventChangeSupport;
using android::emulation::control::WithCallbacks;

using ::goldfish::physics::AmbientEnvironment;
using ::goldfish::physics::BodyModel;
using ::goldfish::physics::InertialModel;

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
class PhysicalModel : public WithCallbacks<EventChangeSupport, PhysicalModelChangeEvent> {
  public:
    PhysicalModel(android::goldfish::Avd* avd);
    ~PhysicalModel() = default;

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
#define SET_TARGET_FUNCTION_NAME(x) setTarget##x
#define PHYSICAL_PARAMETER_(x, y, z, w) \
    void SET_TARGET_FUNCTION_NAME(z)(w value, PhysicalInterpolation mode);

    PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME

    /*
     * Gets current target state of the modeled object.
     */
#define GET_TARGET_FUNCTION_NAME(x) getParameter##x
#define PHYSICAL_PARAMETER_(x, y, z, w) \
    w GET_TARGET_FUNCTION_NAME(z)(ParameterValueType parameterValueType) const;

    PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef GET_TARGET_FUNCTION_NAME

    /*
     * Sensor override methods
     */
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define SENSOR_(x, y, z, v, w) void OVERRIDE_FUNCTION_NAME(z)(v override_value);

    SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_FUNCTION_NAME

    /*
     * Getters for all sensor values.
     * Can be called from any thread.
     */
#define GET_FUNCTION_NAME(x) get##x
#define SENSOR_(x, y, z, v, w) v GET_FUNCTION_NAME(z)(long* measurement_id) const;

    SENSORS_LIST
#undef SENSOR_
#undef GET_FUNCTION_NAME

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
                      int64_t* out_timestamp);

    /**
     * @brief Gets the current foldable device state.
     * @return Current foldable state
     */
    FoldableState getFoldableState();

    /**
     * @brief Checks if the foldable device is currently folded.
     * @return true if device is folded, false otherwise
     */
    bool foldableIsFolded();

    /**
     * @brief Gets the folded area dimensions.
     * @param[out] x X coordinate of folded area
     * @param[out] y Y coordinate of folded area
     * @param[out] w Width of folded area
     * @param[out] h Height of folded area
     * @return true if area was retrieved successfully
     */
    bool getFoldedArea(int* x, int* y, int* w, int* h);

    android::base::EventNotificationSupport<FoldablePostures>* getPostureListener();

  private:
    /*
     * Sets the target value for the given physical parameter that the physical
     * model should move towards.
     * Can be called from any thread.
     */
#define SET_TARGET_INTERNAL_FUNCTION_NAME(x) setTargetInternal##x
#define PHYSICAL_PARAMETER_(x, y, z, w) \
    void SET_TARGET_INTERNAL_FUNCTION_NAME(z)(w value, PhysicalInterpolation mode);

    PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_INTERNAL_FUNCTION_NAME

    /*
     * Getters for non-overridden physical sensor values.
     */
#define GET_PHYSICAL_NAME(x) getPhysical##x
#define SENSOR_(x, y, z, v, w) v GET_PHYSICAL_NAME(z)() const;

    SENSORS_LIST
#undef SENSOR_
#undef GET_PHYSICAL_NAME

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
    template <class T>
    T getSensorValue(const AndroidSensor sensor, const T* overrideMemberPointer,
                     std::function<T()> physicalGetter, long* measurement_id) const {
        const size_t sensorIndex = static_cast<size_t>(sensor);

        std::lock_guard<std::recursive_mutex> lock(mMutex);
        if (mUseOverride[static_cast<size_t>(sensor)]) {
            *measurement_id = mMeasurementId[sensorIndex];
            return *overrideMemberPointer;
        } else {
            if (mIsPhysicalStateChanging) {
                mMeasurementId[sensorIndex]++;
            }
            *measurement_id = mMeasurementId[sensorIndex];
            return physicalGetter();
        }
    }

    void physicalStateChanging();    ///< Called when physical state begins changing
    void physicalStateStabilized();  ///< Called when physical state stabilizes
    void targetStateChanged();       ///< Called when target state changes

    mutable std::recursive_mutex mMutex;  ///< Mutex for thread safety

    InertialModel mInertialModel;            ///< Models inertial motion
    AmbientEnvironment mAmbientEnvironment;  ///< Models ambient conditions
    FoldableModel mFoldableModel;            ///< Models foldable device state
    BodyModel mBodyModel;                    ///< Models body-related sensors

    bool mIsPhysicalStateChanging{false};            ///< True if physical state is changing
    bool isLoadingSnapshot{false};                   ///< True if loading from snapshot
    bool mUseOverride[static_cast<size_t>(AndroidSensor::MAX_SENSORS)] = {false};        ///< Sensor override flags
    mutable long mMeasurementId[static_cast<size_t>(AndroidSensor::MAX_SENSORS)] = {0};  ///< Measurement IDs

#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_(x, y, z, v, w) v OVERRIDE_NAME(z){0.f};

    SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_NAME

    int64_t mModelTimeNs = 0L;  ///< Current model time in nanoseconds
};

}  // namespace physics
}  // namespace android

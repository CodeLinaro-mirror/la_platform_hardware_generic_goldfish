/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <mutex>
#include <vector>

#include "aemu/base/EventNotificationSupport.h"  // for EventNotifi...
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/physics/Physics.h"
#include "goldfish/sensors/Foldable.h"

namespace goldfish::sensors {

class FoldableModel {
  public:
    class PostureListener : public ::android::base::EventNotificationSupport<FoldablePostures> {
        friend class FoldableModel;
    };
    FoldableModel(const android::goldfish::HardwareConfig& hw);

    // called by physical model to set hinge angle.
    // mutex passed from physical model
    void setHingeAngle(uint32_t hingeIndex, float degrees, PhysicalInterpolation mode,
                       std::recursive_mutex& mutex);

    // called by physical model to set hinge posture.
    // mutex passed from physical model
    void setPosture(float posture, PhysicalInterpolation mode, std::recursive_mutex& mutex);

    void setRollable(uint32_t index, float percentage, PhysicalInterpolation mode,
                     std::recursive_mutex& mutex);

    float getHingeAngle(uint32_t hingeIndex,
                        ParameterValueType parameterValueType = ParameterValueType::CURRENT) const;

    float getRollable(uint32_t index, ParameterValueType parameterValueType) const;

    float getPosture(ParameterValueType parameterValueType = ParameterValueType::CURRENT) const;

    FoldableState getFoldableState() const { return mState; }  // structure copy

    bool isFolded() const;

    bool getFoldedArea(int* x, int* y, int* w, int* h) const;

    PostureListener* getPostureListener() { return &mPostureListener; }

  private:
    void initFoldableRoll(const android::goldfish::HardwareConfig& hw);

    FoldableState mState;
    std::vector<AnglesToPosture> mAnglesToPostures;
    PostureListener mPostureListener;
};

}  // namespace goldfish::sensors
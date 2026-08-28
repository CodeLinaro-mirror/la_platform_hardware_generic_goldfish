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

#include <cstdint>

#include "absl/status/status.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/physics/physics.h"

namespace goldfish::physics {

enum class BodyState : std::uint8_t {
    kChanging = 0,
    kStable = 1,
};

class BodyModel {
  public:
    BodyModel() = default;
    /*
     * Sets the body heart rate.
     */
    void SetHeartRate(float bpm, PhysicalInterpolation mode);

    float GetHeartRate(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;

    friend archive::IWriter& operator<<(archive::IWriter&, const BodyModel&);
    friend absl::Status ReadValue(archive::IReader&, BodyModel&);

  private:
    /* BPM */
    static constexpr float kDefaultHeartRate = 0.F;

    float heart_rate_ = kDefaultHeartRate;
};
}  // namespace goldfish::physics

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

#include "goldfish/physics/body_model.h"

namespace goldfish::physics {

void BodyModel::SetHeartRate(float bpm, PhysicalInterpolation /*mode*/) {
    heart_rate_ = bpm;
}

float BodyModel::GetHeartRate(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultHeartRate : heart_rate_;
}

archive::IWriter& operator<<(archive::IWriter& w, const BodyModel& bm) {
    w << bm.heart_rate_;
    return w;
}

absl::Status ReadValue(archive::IReader& r, BodyModel& bm) {
    return ReadValue(r, bm.heart_rate_);
}

}  // namespace goldfish::physics

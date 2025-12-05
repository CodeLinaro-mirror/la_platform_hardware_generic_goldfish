/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "absl/container/inlined_vector.h"

namespace goldfish::sensors {

static constexpr size_t kSensorValueMaxDimensions = 4;
using SensorValue = absl::InlinedVector<float, kSensorValueMaxDimensions>;

struct SensorData {
    size_t measurement_id = 0;
    SensorValue value;
};

}  // namespace goldfish::sensors
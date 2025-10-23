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

#include "goldfish/devices/sensor/FoldableModel.h"

#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "aemu/base/misc/StringUtils.h"

namespace goldfish::devices::sensor {

void FoldableModel::initFoldableRoll(const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_roll) {
        mState.config.numRolls = 0;
        return;
    }

    struct FoldableConfig& config = mState.config;
    FoldableDisplayType type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type);
    if (type >= FoldableDisplayType::TYPE_MAX) {
        type = FoldableDisplayType::HORIZONTAL_ROLL;
    }
    config.type = type;

    // number
    int numRolls = hw.hw_sensor_roll_count;
    if (numRolls < 0 || numRolls > ANDROID_FOLDABLE_MAX_ROLLS) {
        numRolls = 0;
        LOG(WARNING) << "Incorrect roll count " << hw.hw_sensor_roll_count
                     << ", default to 0";
    }
    config.numRolls = numRolls;

    // resize at postures
    config.resizeAtPosture[0] =
            (enum FoldablePostures)hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture;
    config.resizeAtPosture[1] =
            (enum FoldablePostures)hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture;
    config.resizeAtPosture[2] =
            (enum FoldablePostures)hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture;

    // hinge angle ranges and defaults
    std::string rollRanges(hw.hw_sensor_roll_ranges);
    std::string rollDefaults(hw.hw_sensor_roll_defaults);
    std::string rollRadius(hw.hw_sensor_roll_radius);
    std::string rollDirection(hw.hw_sensor_roll_direction);
    std::vector<std::string> rollRangeTokens = absl::StrSplit(rollRanges, ",");
    std::vector<std::string> rollDefaultTokens = absl::StrSplit(rollDefaults, ",");
    std::vector<std::string> rollRadiusTokens = absl::StrSplit(rollRadius, ",");
    std::vector<std::string> rollDirectionTokens = absl::StrSplit(rollDirection, ",");
    if (rollRangeTokens.size() != numRolls || rollDefaultTokens.size() != numRolls ||
        rollRadiusTokens.size() != numRolls || rollDirectionTokens.size() != numRolls) {
        LOG(ERROR) << "Incorrect rollable configs for ranges " << rollRanges << ", defaults "
                   << rollDefaults << ", radius " << rollRadius << ", or directions "
                   << rollDirection;

    } else {
        for (int i = 0; i < numRolls; i++) {
            std::vector<std::string> range = absl::StrSplit(rollRangeTokens[i], "-");
            if (range.size() != 2) {
                LOG(ERROR) << "Incorrect rollable angle range " << rollRangeTokens[i];

            } else {
                config.rollableParams[i] = {
                        .rollRadiusAsDisplayPercent = std::stof(rollRadiusTokens[i]),
                        .displayId = 0,  // TODO: put 0 for now
                        .minRolledPercent = std::stof(range[0]),
                        .maxRolledPercent = std::stof(range[1]),
                        .defaultRolledPercent = std::stof(rollDefaultTokens[i]),
                        .direction = std::stoi(rollDirectionTokens[i]),
                };
            }
        }
    }
    for (unsigned int i = 0; i < mState.config.numRolls; ++i) {
        mState.currentRolledPercent[i] = mState.config.rollableParams[i].defaultRolledPercent;
    }
}

FoldableModel::FoldableModel(const android::goldfish::HardwareConfig& hw) {
    initFoldableRoll(hw);
}

void FoldableModel::setHingeAngle(uint32_t hingeIndex, float degrees, PhysicalInterpolation mode,
                                  std::recursive_mutex& mutex) {
    LOG(WARNING) << "Not yet implemented";
}

void FoldableModel::setPosture(float posture, PhysicalInterpolation mode,
                               std::recursive_mutex& mutex) {
    LOG(WARNING) << "Not yet implemented";
}

void FoldableModel::setRollable(uint32_t index, float percentage, PhysicalInterpolation mode,
                                std::recursive_mutex& mutex) {
    LOG(WARNING) << "Not yet implemented";
}

float FoldableModel::getHingeAngle(uint32_t hingeIndex,
                                   ParameterValueType parameterValueType) const {
    if (hingeIndex >= ANDROID_FOLDABLE_MAX_HINGES) return 0.0f;
    if (hingeIndex >= mState.config.numHinges) return 0.0f;
    return parameterValueType == ParameterValueType::DEFAULT
                   ? mState.config.hingeParams[hingeIndex].defaultDegrees
                   : mState.currentHingeDegrees[hingeIndex];
}

float FoldableModel::getPosture(ParameterValueType parameterValueType) const {
    return parameterValueType == ParameterValueType::DEFAULT ? (float)FoldablePostures::UNKNOWN
                                                             : (float)mState.currentPosture;
}

float FoldableModel::getRollable(uint32_t index, ParameterValueType parameterValueType) const {
    if (index >= ANDROID_FOLDABLE_MAX_ROLLS) return 0.0f;
    if (index >= mState.config.numRolls) return 0.0f;

    return parameterValueType == ParameterValueType::DEFAULT
                   ? mState.config.rollableParams[index].defaultRolledPercent
                   : mState.currentRolledPercent[index];
}

bool FoldableModel::getFoldedArea(int* x, int* y, int* w, int* h) const {
    LOG(WARNING) << "Not yet implemented";
    return false;
}

bool FoldableModel::isFolded() {
    LOG(WARNING) << "Not yet implemented";
    return false;
}

}  // namespace goldfish::devices::sensor

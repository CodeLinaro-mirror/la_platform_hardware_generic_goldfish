// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

constexpr float kPhysicsEpsilon = 0.001F;

inline bool VecNearEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                         float epsilon = kPhysicsEpsilon) {
    return glm::all(glm::epsilonEqual(lhs, rhs, epsilon));
}

inline bool QuaternionNearEqual(const glm::quat& lhs, const glm::quat& rhs,
                                float epsilon = kPhysicsEpsilon) {
    return glm::all(glm::epsilonEqual(lhs, rhs, epsilon)) ||
           glm::all(glm::epsilonEqual(lhs, -rhs, epsilon));
}

inline glm::vec3 ToEulerAnglesXyz(const glm::quat& q) {
    const glm::quat sq(q.w * q.w, q.x * q.x, q.y * q.y, q.z * q.z);

    return {glm::atan(2.0F * (q.x * q.w - q.y * q.z), sq.w - sq.x - sq.y + sq.z),
            glm::asin(glm::clamp(2.0F * (q.x * q.z + q.y * q.w), -1.0F, 1.0F)),
            glm::atan(2.0F * (q.z * q.w - q.x * q.y), sq.w + sq.x - sq.y - sq.z)};
}

inline glm::quat FromEulerAnglesXyz(const glm::vec3& euler) {
    const glm::quat x = glm::angleAxis(euler.x, glm::vec3(1.0F, 0.0F, 0.0F));
    const glm::quat y = glm::angleAxis(euler.y, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::quat z = glm::angleAxis(euler.z, glm::vec3(0.0F, 0.0F, 1.0F));

    return x * y * z;
}

inline glm::vec3 ToEulerAnglesYxz(const glm::quat& q) {
    const glm::quat sq(q.w * q.w, q.x * q.x, q.y * q.y, q.z * q.z);

    return {glm::asin(glm::clamp(2.0F * (q.x * q.w - q.y * q.z), -1.0F, 1.0F)),
            glm::atan(2.0F * (q.x * q.z + q.y * q.w), sq.w - sq.x - sq.y + sq.z),
            glm::atan(2.0F * (q.x * q.y + q.z * q.w), sq.w - sq.x + sq.y - sq.z)};
}

inline glm::quat FromEulerAnglesYxz(const glm::vec3& euler) {
    const glm::quat x = glm::angleAxis(euler.x, glm::vec3(1.0F, 0.0F, 0.0F));
    const glm::quat y = glm::angleAxis(euler.y, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::quat z = glm::angleAxis(euler.z, glm::vec3(0.0F, 0.0F, 1.0F));

    return y * x * z;
}

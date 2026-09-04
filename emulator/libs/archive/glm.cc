/* Copyright (C) 2026 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "goldfish/archive/glm.h"

#include "android/status/status_macros.h"

namespace goldfish::archive {

absl::Status ReadValue(archive::IReader& r, glm::mat4x3& x) {
    for (unsigned col = 0; col < x.length(); ++col) {
        for (unsigned row = 0; row < x[col].length(); ++row) {
            RETURN_IF_ERROR(ReadValue(r, x[col][row]));
        }
    }
    return absl::OkStatus();
}

absl::Status ReadValue(archive::IReader& r, glm::vec2& x) {
    return ReadValue(r, x[0], x[1]);
}

absl::Status ReadValue(archive::IReader& r, glm::vec3& x) {
    return ReadValue(r, x[0], x[1], x[2]);
}

absl::Status ReadValue(archive::IReader& r, glm::vec4& x) {
    return ReadValue(r, x[0], x[1], x[2], x[3]);
}

IWriter& operator<<(IWriter& w, const glm::mat4x3& x) {
    for (unsigned col = 0; col < x.length(); ++col) {
        for (unsigned row = 0; row < x[col].length(); ++row) {
            w << x[col][row];
        }
    }

    return w;
}

IWriter& operator<<(IWriter& w, const glm::vec2& x) {
    return w << x[0] << x[1];
}

IWriter& operator<<(IWriter& w, const glm::vec3& x) {
    return w << x[0] << x[1] << x[2];
}

IWriter& operator<<(IWriter& w, const glm::vec4& x) {
    return w << x[0] << x[1] << x[2] << x[3];
}

}  // namespace goldfish::archive

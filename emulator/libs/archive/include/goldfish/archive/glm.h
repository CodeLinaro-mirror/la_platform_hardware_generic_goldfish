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

#pragma once

#include <glm/mat4x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::archive {

absl::Status ReadValue(archive::IReader& r, glm::mat4x3&);
absl::Status ReadValue(archive::IReader& r, glm::vec2&);
absl::Status ReadValue(archive::IReader& r, glm::vec3&);
absl::Status ReadValue(archive::IReader& r, glm::vec4&);

IWriter& operator<<(IWriter& w, const glm::mat4x3&);
IWriter& operator<<(IWriter& w, const glm::vec2&);
IWriter& operator<<(IWriter& w, const glm::vec3&);
IWriter& operator<<(IWriter& w, const glm::vec4&);

}  // namespace goldfish::archive

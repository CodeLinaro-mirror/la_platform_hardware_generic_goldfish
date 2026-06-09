/* Copyright (C) 2025 The Android Open Source Project
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

#include "absl/status/status.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::snapshottable {

struct Snapshottable {
    virtual ~Snapshottable() = default;

    virtual void OnPreSave() {}
    virtual absl::Status OnSave(archive::IWriter&) const = 0;
    virtual void OnPostSave() {}
    virtual void OnPreLoad() {}
    virtual absl::Status OnLoad(archive::IReader&) = 0;
    virtual absl::Status OnPostLoad() { return absl::OkStatus(); }
};

}  // namespace goldfish::snapshottable

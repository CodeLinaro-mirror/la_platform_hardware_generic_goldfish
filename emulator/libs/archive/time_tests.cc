// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/time.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::Not;

using goldfish::archive::DequeArchive;

TEST(time, positive) {
    const absl::Time t = absl::FromUnixMicros(1780354538123456ULL);

    DequeArchive archive;

    archive << t;
    EXPECT_THAT(ReadValue<absl::Time>(archive), IsOkAndHolds(t));
}

TEST(time, negative) {
    DequeArchive archive;

    EXPECT_THAT(ReadValue<absl::Time>(archive), Not(IsOk()));
}

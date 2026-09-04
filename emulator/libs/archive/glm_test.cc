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

#include "goldfish/archive/glm.h"

#include <glm/mat4x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/deque_archive.h"

namespace goldfish::archive {

using ::absl_testing::IsOk;
using ::testing::Not;

TEST(glm, vec2_positive) {
    const glm::vec2 v1(1.5f, -2.5f);
    const glm::vec2 v2(0.0f, 42.125f);

    DequeArchive archive;
    archive << v1 << v2;

    glm::vec2 read1;
    ASSERT_THAT(ReadValue(archive, read1), IsOk());
    EXPECT_EQ(read1, v1);

    glm::vec2 read2;
    ASSERT_THAT(ReadValue(archive, read2), IsOk());
    EXPECT_EQ(read2, v2);

    EXPECT_TRUE(archive.Empty());
}

TEST(glm, vec2_negative) {
    // 1. Completely empty archive
    {
        DequeArchive archive;
        glm::vec2 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }

    // 2. Truncated stream (only 1 float written, 2 expected)
    {
        DequeArchive archive;
        archive << 1.0f;

        glm::vec2 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }
}

TEST(glm, vec3_positive) {
    const glm::vec3 v1(1.0f, -2.5f, 3.75f);
    const glm::vec3 v2(0.0f, 0.0f, 0.0f);

    DequeArchive archive;
    archive << v1 << v2;

    glm::vec3 read1;
    ASSERT_THAT(ReadValue(archive, read1), IsOk());
    EXPECT_EQ(read1, v1);

    glm::vec3 read2;
    ASSERT_THAT(ReadValue(archive, read2), IsOk());
    EXPECT_EQ(read2, v2);

    EXPECT_TRUE(archive.Empty());
}

TEST(glm, vec3_negative) {
    // 1. Completely empty archive
    {
        DequeArchive archive;
        glm::vec3 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }

    // 2. Truncated stream (only 2 floats written, 3 expected)
    {
        DequeArchive archive;
        archive << 1.0f << 2.0f;

        glm::vec3 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }
}

TEST(glm, vec4_positive) {
    const glm::vec4 v1(1.0f, -2.5f, 3.75f, -4.125f);
    const glm::vec4 v2(10.0f, 20.0f, 30.0f, 40.0f);

    DequeArchive archive;
    archive << v1 << v2;

    glm::vec4 read1;
    ASSERT_THAT(ReadValue(archive, read1), IsOk());
    EXPECT_EQ(read1, v1);

    glm::vec4 read2;
    ASSERT_THAT(ReadValue(archive, read2), IsOk());
    EXPECT_EQ(read2, v2);

    EXPECT_TRUE(archive.Empty());
}

TEST(glm, vec4_negative) {
    // 1. Completely empty archive
    {
        DequeArchive archive;
        glm::vec4 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }

    // 2. Truncated stream (only 3 floats written, 4 expected)
    {
        DequeArchive archive;
        archive << 1.0f << 2.0f << 3.0f;

        glm::vec4 read_vec;
        EXPECT_THAT(ReadValue(archive, read_vec), Not(IsOk()));
    }
}

TEST(glm, mat4x3_positive) {
    const glm::mat4x3 mat(1.0f, 2.0f, 3.0f,    // col 0
                          4.0f, 5.0f, 6.0f,    // col 1
                          7.0f, 8.0f, 9.0f,    // col 2
                          10.0f, 11.0f, 12.0f  // col 3
    );

    DequeArchive archive;
    archive << mat;

    glm::mat4x3 read_mat;
    ASSERT_THAT(ReadValue(archive, read_mat), IsOk());
    EXPECT_EQ(read_mat, mat);
    EXPECT_TRUE(archive.Empty());
}

TEST(glm, mat4x3_negative) {
    // 1. Completely empty archive
    {
        DequeArchive archive;
        glm::mat4x3 read_mat;
        EXPECT_THAT(ReadValue(archive, read_mat), Not(IsOk()));
    }

    // 2. Truncated stream (only 11 floats written, 12 expected)
    {
        DequeArchive archive;
        for (int i = 0; i < 11; ++i) {
            archive << static_cast<float>(i);
        }

        glm::mat4x3 read_mat;
        EXPECT_THAT(ReadValue(archive, read_mat), Not(IsOk()));
    }
}

TEST(glm, mixed_stream) {
    const glm::vec2 v2(1.0f, 2.0f);
    const glm::vec3 v3(3.0f, 4.0f, 5.0f);
    const glm::vec4 v4(6.0f, 7.0f, 8.0f, 9.0f);
    const glm::mat4x3 mat(1.1f, 1.2f, 1.3f, 2.1f, 2.2f, 2.3f, 3.1f, 3.2f, 3.3f, 4.1f, 4.2f, 4.3f);

    DequeArchive archive;
    archive << v2 << v3 << v4 << mat;

    glm::vec2 read_v2;
    glm::vec3 read_v3;
    glm::vec4 read_v4;
    glm::mat4x3 read_mat;

    ASSERT_THAT(ReadValue(archive, read_v2), IsOk());
    ASSERT_THAT(ReadValue(archive, read_v3), IsOk());
    ASSERT_THAT(ReadValue(archive, read_v4), IsOk());
    ASSERT_THAT(ReadValue(archive, read_mat), IsOk());

    EXPECT_EQ(read_v2, v2);
    EXPECT_EQ(read_v3, v3);
    EXPECT_EQ(read_v4, v4);
    EXPECT_EQ(read_mat, mat);
    EXPECT_TRUE(archive.Empty());
}

}  // namespace goldfish::archive
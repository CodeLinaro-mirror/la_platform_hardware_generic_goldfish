// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "core/linux/nvenc_loader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace goldfish::videobridge {
namespace {

using ::testing::HasSubstr;

TEST(NvencLoaderTest, NonExistentLibraryReturnsNotFoundWithRemediation) {
    auto result = NvencLoader::Create("non_existent_library_12345.so");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
    EXPECT_THAT(result.status().message(), HasSubstr("Remediation"));
    EXPECT_THAT(result.status().message(), HasSubstr("libnvidia-encode.so.1"));
}

TEST(NvencLoaderTest, CreateForTestReturnsValidLoader) {
    NV_ENCODE_API_FUNCTION_LIST mock_api{};
    mock_api.version = NV_ENCODE_API_FUNCTION_LIST_VER;

    auto result = NvencLoader::CreateForTest(mock_api);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(*result, nullptr);
    EXPECT_EQ((*result)->api().version, NV_ENCODE_API_FUNCTION_LIST_VER);
}

TEST(NvencLoaderTest, StatusToStringTranslatesStatusCodesCorrectly) {
    EXPECT_THAT(NvencStatusToString(NV_ENC_SUCCESS), HasSubstr("NV_ENC_SUCCESS"));
    EXPECT_THAT(NvencStatusToString(NV_ENC_ERR_NO_ENCODE_DEVICE),
                HasSubstr("NV_ENC_ERR_NO_ENCODE_DEVICE"));
    EXPECT_THAT(NvencStatusToString(NV_ENC_ERR_OUT_OF_MEMORY),
                HasSubstr("NV_ENC_ERR_OUT_OF_MEMORY"));
    EXPECT_THAT(NvencStatusToString(NV_ENC_ERR_INVALID_PARAM),
                HasSubstr("NV_ENC_ERR_INVALID_PARAM"));
    EXPECT_THAT(NvencStatusToString(static_cast<NVENCSTATUS>(999)),
                HasSubstr("NVENC_UNKNOWN_ERROR (999)"));
}

}  // namespace
}  // namespace goldfish::videobridge

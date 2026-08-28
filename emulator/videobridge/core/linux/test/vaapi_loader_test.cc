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
#include "core/linux/vaapi_loader.h"

#include <gtest/gtest.h>

#include "core/linux/test/fake_vaapi_driver.h"

namespace goldfish::videobridge {
namespace {

TEST(VaapiLoaderTest, StatusToStringTranslatesAllCodes) {
    EXPECT_NE(VaapiStatusToString(VA_STATUS_SUCCESS).find("Operation succeeded"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_OPERATION_FAILED)
                      .find("General driver operation failure"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_ALLOCATION_FAILED)
                      .find("Out of system or GPU memory"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_DISPLAY).find("Invalid VADisplay"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_CONFIG).find("Invalid VAConfigID"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_CONTEXT).find("Invalid VAContextID"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_SURFACE).find("Invalid VASurfaceID"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_BUFFER).find("Invalid VABufferID"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_IMAGE).find("Invalid VAImageID"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_INVALID_PARAMETER).find("Invalid parameter"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_UNSUPPORTED_PROFILE)
                      .find("not supported by hardware ASIC"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT)
                      .find("entrypoint not supported"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE)
                      .find("Buffer type not supported"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_SURFACE_BUSY).find("Surface is currently in use"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_UNIMPLEMENTED).find("not implemented"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(VA_STATUS_ERROR_NOT_ENOUGH_BUFFER)
                      .find("Allocated buffer too small"),
              std::string::npos);
    EXPECT_NE(VaapiStatusToString(static_cast<VAStatus>(9999)).find("VA_STATUS_UNKNOWN"),
              std::string::npos);
}

TEST(VaapiLoaderTest, CreateFailsGracefullyWithActionableErrorWhenMissing) {
    auto loader_status = VaapiLoader::Create("/nonexistent/path/libva.so.2");
    EXPECT_FALSE(loader_status.ok());
    EXPECT_EQ(loader_status.status().code(), absl::StatusCode::kNotFound);
    EXPECT_NE(loader_status.status().message().find(
                      "Remediation: Ensure Intel Media Driver (iHD) or Mesa VA-API"),
              std::string::npos);
}

TEST(VaapiLoaderTest, CreateForTestSucceedsWithMockDriver) {
    FakeVaapiDriver driver;
    auto fn_list = driver.CreateFunctionList();
    auto loader_status = VaapiLoader::CreateForTest(fn_list, driver.display());
    ASSERT_TRUE(loader_status.ok());
    auto loader = std::move(*loader_status);
    ASSERT_NE(loader, nullptr);
    EXPECT_EQ(loader->display(), driver.display());
    EXPECT_NE(loader->api().vaInitialize, nullptr);
}

}  // namespace
}  // namespace goldfish::videobridge

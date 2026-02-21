// Copyright 2025 The Android Open Source Project
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

#include "android/crashreport/binary_annotation.h"

#include <gtest/gtest.h>

#include "grpc_diagnostic.pb.h"

#include "android/crashreport/thread.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/circular_message_log.h"

namespace android::crashreport {

using android::control::interceptor::GrpcBreadcrumb;
using goldfish::proto_data_store::ProtoCircularLog;

TEST(BinaryAnnotationTest, Basic) {
    BinaryAnnotation<1024> annotation("test_binary");
    EXPECT_EQ(annotation.size(), 1024u);
    EXPECT_NE(annotation.Data(), nullptr);
    EXPECT_STREQ(annotation.name(), "test_binary");
}

TEST(BinaryAnnotationTest, IntegrationWithProtoLog) {
    // 1. Create a binary annotation.
    BinaryAnnotation<2048> annotation("grpc_log");

    // 2. Initialize a ProtoCircularLog over the annotation's buffer.
    ASSERT_OK_AND_ASSIGN(auto log, ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(
                                           annotation.Data(), annotation.size()));

    // 3. Push data.
    GrpcBreadcrumb crumb;
    crumb.set_call_id(999);
    ASSERT_OK(log->Push(crumb));

    // 4. Verify data can be read back from the same memory.
    int seen = 0;
    log->ForEach([&](const GrpcBreadcrumb& msg) {
        if (msg.call_id() == 999) seen++;
        return true;
    });

    EXPECT_EQ(seen, 1);
    EXPECT_EQ(log->MessageCount(), 1);
}

}  // namespace android::crashreport

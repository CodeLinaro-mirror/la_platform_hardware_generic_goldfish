// Copyright 2026 The Android Open Source Project
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

#include "android/crashreport/breadcrumb_proto.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "absl/status/status.h"

#include "android/crashreport/looper_registrations.h"
#include "android/status/status_matcher_macros.h"

namespace android::crashreport {

TEST(BreadcrumbProtoTest, LogValidBreadcrumb) {
    auto status = LogBreadcrumb(BreadcrumbType::kGrpc, 12345, BreadcrumbPhase::kInstant,
                                PayloadType::kGrpcProto, "test payload");
    EXPECT_OK(status);
}

TEST(BreadcrumbProtoTest, LogInvalidBreadcrumbType) {
    auto invalid_type = static_cast<BreadcrumbType>(999);
    auto status = LogBreadcrumb(invalid_type, 12345, BreadcrumbPhase::kInstant,
                                PayloadType::kGrpcProto, "test payload");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(), "Invalid breadcrumb type: 999");
}

TEST(LooperRegistrationsTest, FreshRegistrationAppendsAndIndexes) {
    LooperRegistrationsAnnotation<256> ann("test_loopers");
    EXPECT_EQ(ann.size(), 0);

    EXPECT_EQ(ann.GetOrRegister("QemuLoop"), 1);
    EXPECT_EQ(std::string_view(static_cast<const char*>(ann.value()), ann.size()), "1=QemuLoop;");
}

TEST(LooperRegistrationsTest, DuplicateRegistrationIsDeduplicated) {
    LooperRegistrationsAnnotation<256> ann("test_loopers");
    EXPECT_EQ(ann.GetOrRegister("ConsoleServerLoop"), 1);
    size_t size_after_first = ann.size();

    // Re-registering the same name must return the existing ID and leave the buffer untouched.
    EXPECT_EQ(ann.GetOrRegister("ConsoleServerLoop"), 1);
    EXPECT_EQ(ann.size(), size_after_first);
    EXPECT_EQ(std::string_view(static_cast<const char*>(ann.value()), ann.size()),
              "1=ConsoleServerLoop;");
}

TEST(LooperRegistrationsTest, MultipleDistinctRegistrations) {
    LooperRegistrationsAnnotation<256> ann("test_loopers");
    EXPECT_EQ(ann.GetOrRegister("LoopOne"), 1);
    EXPECT_EQ(ann.GetOrRegister("LoopTwo"), 2);
    EXPECT_EQ(ann.GetOrRegister("LoopOne"), 1);

    EXPECT_EQ(std::string_view(static_cast<const char*>(ann.value()), ann.size()),
              "1=LoopOne;2=LoopTwo;");
}

TEST(LooperRegistrationsTest, BorrowedStringViewRemainsValidAfterCallerDestruction) {
    LooperRegistrationsAnnotation<256> ann("test_loopers");
    {
        std::string ephemeral_name = "EphemeralLoop";
        EXPECT_EQ(ann.GetOrRegister(ephemeral_name), 1);
    }
    // ephemeral_name has been deallocated; re-registering queries internal key borrowed from
    // buffer_.
    EXPECT_EQ(ann.GetOrRegister("EphemeralLoop"), 1);
    EXPECT_EQ(std::string_view(static_cast<const char*>(ann.value()), ann.size()),
              "1=EphemeralLoop;");
}

TEST(LooperRegistrationsTest, BufferLimitExceeded) {
    // kMaxFormatOverhead is 6. "1=ShortName;" takes 12 chars + overhead.
    // Annotation of size 14 cannot fit "1=ShortName;".
    LooperRegistrationsAnnotation<14> ann("small_buf");
    ann.GetOrRegister("ShortName");
    EXPECT_EQ(ann.size(), 0);
}

TEST(LooperRegistrationsTest, ConcurrentRegistrations) {
    LooperRegistrationsAnnotation<4096> ann("concurrent_loopers");
    constexpr int kNumThreads = 4;
    constexpr int kIterations = 20;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&ann, t]() {
            for (int i = 0; i < kIterations; ++i) {
                // Thread-specific loop name
                std::string unique_name = absl::StrCat("ThreadLoop_", t, "_", i);
                ann.GetOrRegister(unique_name);

                // Shared name contended across all threads
                ann.GetOrRegister("ContendedConsoleLoop");
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Ensure contended loop was successfully registered and deduplicated
    std::string_view buf(static_cast<const char*>(ann.value()), ann.size());
    EXPECT_TRUE(buf.find("=ContendedConsoleLoop;") != std::string_view::npos);
}

TEST(LooperRegistrationsTest, IdClampingAtUint8Max) {
    // 255 entries with short names: "1=L1;2=L2;...255=L255;" takes ~1.5KB.
    LooperRegistrationsAnnotation<4096> ann("clamped_loopers");
    for (int i = 1; i <= 254; ++i) {
        EXPECT_EQ(ann.GetOrRegister(absl::StrCat("L", i)), i);
    }
    EXPECT_EQ(ann.GetOrRegister("Loop255"), 255);

    // Once next_loop_id reaches 255, subsequent registrations clamp at 255 without wrapping.
    EXPECT_EQ(ann.GetOrRegister("LoopOverflow"), 255);
}

TEST(LooperRegistrationsTest, ConcurrentGetOrRegisterSameNameRacesToSameId) {
    LooperRegistrationsAnnotation<4096> ann("concurrent_same");
    constexpr int kNumThreads = 8;
    constexpr int kIterations = 100;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&ann]() {
            for (int i = 0; i < kIterations; ++i) {
                uint8_t id = ann.GetOrRegister("SharedConsoleLoop");
                EXPECT_EQ(id, 1);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(std::string_view(static_cast<const char*>(ann.value()), ann.size()),
              "1=SharedConsoleLoop;");
}

TEST(LooperRegistrationsTest, GlobalRegistrationFunction) {
    constexpr std::string_view kGlobalLoopName = "TestGlobalUniqueLoop";

    uint8_t auto_id1 = RegisterLooper(kGlobalLoopName);
    EXPECT_GT(auto_id1, 0);

    // Duplicate call returns the exact same ID
    uint8_t auto_id2 = RegisterLooper(kGlobalLoopName);
    EXPECT_EQ(auto_id1, auto_id2);
}

}  // namespace android::crashreport

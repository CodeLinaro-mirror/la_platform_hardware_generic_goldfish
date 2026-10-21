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

#include "goldfish/update_check/update_checker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/http/web_server.h"
#include "goldfish/network/http_client.h"

namespace goldfish::update_check {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::goldfish::http::CreateWebServer;
using ::goldfish::http::HttpMethod;
using ::goldfish::http::HttpRequest;
using ::goldfish::http::HttpResponse;
using ::goldfish::http::HttpResponseWriter;
using ::goldfish::http::HttpStatus;
using ::goldfish::http::WebServer;
using ::testing::HasSubstr;

constexpr std::string_view kManifestXml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<sdk:sdk-repository xmlns:sdk="http://schemas.android.com/sdk/android/repo/repository2/03">
  <channel id="channel-2">dev</channel>
  <remotePackage path="emulator">
    <revision><major>37</major><minor>3</minor><micro>3</micro></revision>
    <channelRef ref="channel-2"/>
    <archives><archive><host-os>linux</host-os><host-arch>x64</host-arch></archive></archives>
  </remotePackage>
  <remotePackage path="emulators;16173978">
    <revision><major>37</major><minor>1</minor><micro>2</micro></revision>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>
  <remotePackage path="emulators;16373346">
    <revision><major>37</major><minor>1</minor><micro>3</micro></revision>
    <channelRef ref="channel-2"/>
    <archives>
      <archive><host-os>linux</host-os><host-arch>x64</host-arch></archive>
      <archive><host-os>macosx</host-os><host-arch>aarch64</host-arch></archive>
      <archive><host-os>windows</host-os><host-arch>x64</host-arch></archive>
    </archives>
  </remotePackage>
</sdk:sdk-repository>
)xml";

class FakeHttpFetcher final : public goldfish::network::HttpClient {
  public:
    FakeHttpFetcher() : response_(std::string(kManifestXml)) {}
    explicit FakeHttpFetcher(absl::StatusOr<std::string> response)
            : response_(std::move(response)) {}

    absl::StatusOr<std::string> Get(std::string_view url, absl::Duration timeout,
                                    const std::function<bool()>& /*cancel*/) override {
        ++call_count_;
        last_url_ = std::string(url);
        last_timeout_ = timeout;
        return response_;
    }

    absl::StatusOr<std::string> Post(std::string_view /*url*/, std::string_view /*body*/,
                                     absl::Span<const std::string_view> /*headers*/,
                                     absl::Duration /*timeout*/,
                                     const std::function<bool()>& /*cancel*/) override {
        return absl::UnimplementedError("Post not used by update_check");
    }

    void SetResponse(absl::StatusOr<std::string> response) { response_ = std::move(response); }

    int call_count() const { return call_count_; }
    const std::string& last_url() const { return last_url_; }
    absl::Duration last_timeout() const { return last_timeout_; }

  private:
    absl::StatusOr<std::string> response_;
    int call_count_ = 0;
    std::string last_url_;
    absl::Duration last_timeout_ = absl::ZeroDuration();
};

UpdateCheckOptions BaseTestOptions(const std::filesystem::path& user_dir) {
    return UpdateCheckOptions{
        .user_directory = user_dir,
        .base_url = "https://dl.google.com/android/repository/repository2-3.xml",
        .package_prefix = "emulators;",
        .package_display_name = "Android Emulator (Preview)",
        .emulator_version = "37.1.2",
        .build_id = "16173978",
        .platform = "linux",
        .target_cpu = "x86_64",
        .core_version = "10.2.90 (v10.2.0)",
        .user_id = "0f8fad5b-d9cb-469f-a165-70867728950e",
        .channel = UpdateChannel::kDev,
        .interval = absl::Hours(4),
        .http_timeout = absl::Seconds(20),
    };
}

TEST(UpdateCheckerTest, FormatsLegacyCompatibleUrl) {
    android::base::TestTempDir temp_dir("update_check_url_test");
    auto opts = BaseTestOptions(temp_dir.path);
    opts.emulator_version = "37.1.3";
    opts.build_id = "16373346";
    FakeHttpFetcher fetcher;

    const absl::Time t0 = absl::FromUnixSeconds(1700000000);
    ASSERT_OK_AND_ASSIGN(auto notice, CheckForUpdate(opts, fetcher, t0));
    EXPECT_EQ(notice, std::nullopt);
    EXPECT_EQ(fetcher.last_url(),
              "https://dl.google.com/android/repository/repository2-3.xml"
              "?tool=emulator&uid=0f8fad5b-d9cb-469f-a165-70867728950e"
              "&os=Linux&osa=x86_64&version=37.1.3.0&coreVersion=10.2.90%20%28v10.2.0%29");
}

TEST(UpdateCheckerTest, ReportsNewerVersionAndThrottlesForFourHours) {
    android::base::TestTempDir temp_dir("update_check_throttle_test");
    auto opts = BaseTestOptions(temp_dir.path);
    FakeHttpFetcher fetcher;

    const absl::Time t0 = absl::FromUnixSeconds(1700000000);
    ASSERT_OK_AND_ASSIGN(auto notice1, CheckForUpdate(opts, fetcher, t0));
    ASSERT_TRUE(notice1.has_value());
    EXPECT_THAT(*notice1, HasSubstr("A newer Android Emulator (Preview) is available: "
                                    "37.1.3 (build 16373346). You are running "
                                    "37.1.2 (build 16173978)."));
    EXPECT_THAT(*notice1, HasSubstr("sdkmanager \"emulators;latest\""));
    EXPECT_EQ(fetcher.call_count(), 1);

    // Within 4 hours (t0 + 3h 59m): check is throttled without hitting the network.
    ASSERT_OK_AND_ASSIGN(auto notice2,
                         CheckForUpdate(opts, fetcher, t0 + absl::Hours(3) + absl::Minutes(59)));
    EXPECT_EQ(notice2, std::nullopt);
    EXPECT_EQ(fetcher.call_count(), 1);

    // At 4 hours (t0 + 4h): check runs again.
    ASSERT_OK_AND_ASSIGN(auto notice3, CheckForUpdate(opts, fetcher, t0 + absl::Hours(4)));
    EXPECT_TRUE(notice3.has_value());
    EXPECT_EQ(fetcher.call_count(), 2);

    // If the system clock moves backward before the recorded timestamp (t0 - 1h),
    // the check runs rather than staying throttled until real time catches up.
    ASSERT_OK_AND_ASSIGN(auto notice4, CheckForUpdate(opts, fetcher, t0 - absl::Hours(1)));
    EXPECT_TRUE(notice4.has_value());
    EXPECT_EQ(fetcher.call_count(), 3);
}

TEST(UpdateCheckerTest, UpToDateAndNewerCiBuildsDoNotReportUpdate) {
    android::base::TestTempDir temp_dir("update_check_uptodate_test");
    FakeHttpFetcher fetcher;

    // Running exact latest build (37.1.3-16373346).
    auto opts = BaseTestOptions(temp_dir.path);
    opts.emulator_version = "37.1.3";
    opts.build_id = "16373346";
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    ASSERT_OK_AND_ASSIGN(auto notice1, CheckForUpdate(opts, fetcher, t0));
    EXPECT_EQ(notice1, std::nullopt);

    // Running a newer CI build of 37.1.3 (16500000 > 16373346) 5 hours later.
    opts.build_id = "16500000";
    ASSERT_OK_AND_ASSIGN(auto notice2, CheckForUpdate(opts, fetcher, t0 + absl::Hours(5)));
    EXPECT_EQ(notice2, std::nullopt);

    // Running 37.1.2 on Stable channel skips Dev channel updates (channel-2 > kStable).
    opts.emulator_version = "37.1.2";
    opts.build_id = "16173978";
    opts.channel = UpdateChannel::kStable;
    ASSERT_OK_AND_ASSIGN(auto notice3, CheckForUpdate(opts, fetcher, t0 + absl::Hours(10)));
    EXPECT_EQ(notice3, std::nullopt);
}

TEST(UpdateCheckerTest, DeveloperAndPresubmitBuildsAreSkipped) {
    android::base::TestTempDir temp_dir("update_check_dev_test");
    FakeHttpFetcher fetcher;

    auto opts = BaseTestOptions(temp_dir.path);
    opts.build_id = "developer";
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    ASSERT_OK_AND_ASSIGN(auto res_dev, CheckForUpdate(opts, fetcher, t0));
    EXPECT_EQ(res_dev, std::nullopt);
    EXPECT_EQ(fetcher.call_count(), 0);
    EXPECT_FALSE(std::filesystem::exists(temp_dir.path / kUpdateCheckIniFileName));

    opts.build_id = "P12345678";
    ASSERT_OK_AND_ASSIGN(auto res_presubmit, CheckForUpdate(opts, fetcher, t0));
    EXPECT_EQ(res_presubmit, std::nullopt);
    EXPECT_EQ(fetcher.call_count(), 0);

    opts.build_id = "16173978";
    opts.emulator_version = "invalid";
    ASSERT_OK_AND_ASSIGN(auto res_invalid_ver, CheckForUpdate(opts, fetcher, t0));
    EXPECT_EQ(res_invalid_ver, std::nullopt);
    EXPECT_EQ(fetcher.call_count(), 0);

    // StartAsyncUpdateCheck also returns immediately for non-release builds without touching disk.
    StartAsyncUpdateCheck(opts);
    EXPECT_FALSE(std::filesystem::exists(temp_dir.path / kUpdateCheckIniFileName));
}

TEST(UpdateCheckerTest, RejectsUnsupportedHostPlatformOrCpu) {
    android::base::TestTempDir temp_dir("update_check_host_test");
    FakeHttpFetcher fetcher;
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    auto opts = BaseTestOptions(temp_dir.path);
    opts.platform.clear();
    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kFailedPrecondition));

    opts.platform = "unknown";
    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kFailedPrecondition));

    opts.platform = "linux";
    opts.target_cpu.clear();
    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kFailedPrecondition));

    opts.target_cpu = "riscv64";
    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(UpdateCheckerTest, NetworkFailureDoesNotRecordCheckTimestamp) {
    android::base::TestTempDir temp_dir("update_check_failure_test");
    auto opts = BaseTestOptions(temp_dir.path);
    FakeHttpFetcher fetcher(absl::UnavailableError("offline"));
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_EQ(fetcher.call_count(), 1);

    // A subsequent check 1 minute later should still run because the failed check
    // did not record a timestamp.
    fetcher.SetResponse(std::string(kManifestXml));
    ASSERT_OK_AND_ASSIGN(auto retry_notice, CheckForUpdate(opts, fetcher, t0 + absl::Minutes(1)));
    EXPECT_TRUE(retry_notice.has_value());
    EXPECT_EQ(fetcher.call_count(), 2);
}

TEST(UpdateCheckerTest, InvalidManifestXmlReturnsErrorWithoutRecordingCheckTimestamp) {
    android::base::TestTempDir temp_dir("update_check_invalid_xml_test");
    auto opts = BaseTestOptions(temp_dir.path);
    FakeHttpFetcher fetcher("<invalid-xml");
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    EXPECT_THAT(CheckForUpdate(opts, fetcher, t0), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_EQ(fetcher.call_count(), 1);
    EXPECT_FALSE(std::filesystem::exists(temp_dir.path / kUpdateCheckIniFileName));

    fetcher.SetResponse(std::string(kManifestXml));
    ASSERT_OK_AND_ASSIGN(auto retry_notice, CheckForUpdate(opts, fetcher, t0 + absl::Minutes(1)));
    EXPECT_TRUE(retry_notice.has_value());
    EXPECT_EQ(fetcher.call_count(), 2);
}

TEST(UpdateCheckerTest, MapsDarwinAndWindowsHostsAndHandlesEdgeCaseOptions) {
    FakeHttpFetcher fetcher;
    const absl::Time t0 = absl::FromUnixSeconds(1700000000);

    // Empty user_directory disables timestamp persistence/throttling;
    // 4-part emulator_version is preserved without appending an extra ".0".
    auto darwin_opts = BaseTestOptions(/*user_dir=*/{});
    darwin_opts.platform = "darwin";
    darwin_opts.target_cpu = "arm64";
    darwin_opts.emulator_version = "37.1.2.0";
    ASSERT_OK_AND_ASSIGN(auto darwin_notice, CheckForUpdate(darwin_opts, fetcher, t0));
    ASSERT_TRUE(darwin_notice.has_value());
    EXPECT_THAT(*darwin_notice, HasSubstr("37.1.3 (build 16373346)"));
    EXPECT_THAT(fetcher.last_url(), HasSubstr("&os=Mac&osa=arm64&version=37.1.2.0&"));

    // Empty core_version omits query parameters; windows with x86_64 target_cpu
    // maps to ("windows", "x64").
    auto win_opts = BaseTestOptions(/*user_dir=*/{});
    win_opts.platform = "windows";
    win_opts.target_cpu = "x86_64";
    win_opts.core_version.clear();
    ASSERT_OK_AND_ASSIGN(auto win_notice, CheckForUpdate(win_opts, fetcher, t0));
    ASSERT_TRUE(win_notice.has_value());
    EXPECT_THAT(*win_notice, HasSubstr("37.1.3 (build 16373346)"));
    EXPECT_EQ(fetcher.last_url(), win_opts.base_url);
}

TEST(UpdateCheckerTest, StartAsyncUpdateCheckFetchesAndCancelsCleanly) {
    android::base::TestTempDir temp_dir("update_check_async_test");
    std::atomic<int> manifest_requests{0};
    std::atomic<bool> stall_entered{false};

    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnGet("/repository2-3.xml", [&](const HttpRequest&) {
        manifest_requests.fetch_add(1, std::memory_order_relaxed);
        return HttpResponse::String(std::string(kManifestXml), HttpStatus::kOk);
    });
    server.OnStream(HttpMethod::kGet, "/stall.xml",
                    [&](const HttpRequest&, std::shared_ptr<HttpResponseWriter> /*writer*/) {
                        stall_entered.store(true, std::memory_order_relaxed);
                    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string server_base = absl::StrCat(
            "http://127.0.0.1:", goldfish::network::GetPortFromEndpoint(server.GetEndpoint()));

    // 1. Successful background update check writes the timestamp INI file.
    {
        auto opts = BaseTestOptions(temp_dir.path);
        opts.base_url = absl::StrCat(server_base, "/repository2-3.xml");
        ScopedUpdateCheck handle;
        handle = StartAsyncUpdateCheck(std::move(opts));
        handle.Wait();
    }
    EXPECT_EQ(manifest_requests.load(std::memory_order_relaxed), 1);
    EXPECT_TRUE(std::filesystem::exists(temp_dir.path / kUpdateCheckIniFileName));

    // 2. Destroying ScopedUpdateCheck while a request is stalled cancels the HTTP
    // transfer promptly instead of waiting for the full 5s http_timeout.
    android::base::TestTempDir stall_dir("update_check_async_stall_test");
    auto stall_opts = BaseTestOptions(stall_dir.path);
    stall_opts.base_url = absl::StrCat(server_base, "/stall.xml");
    stall_opts.http_timeout = absl::Seconds(5);

    const absl::Time start = absl::Now();
    {
        ScopedUpdateCheck handle = StartAsyncUpdateCheck(std::move(stall_opts));
        while (!stall_entered.load(std::memory_order_relaxed) &&
               (absl::Now() - start) < absl::Seconds(2)) {
            absl::SleepFor(absl::Milliseconds(10));
        }
    }
    const absl::Duration elapsed = absl::Now() - start;
    server.Stop();

    EXPECT_TRUE(stall_entered.load(std::memory_order_relaxed));
    EXPECT_LT(elapsed, absl::Seconds(4));
    EXPECT_FALSE(std::filesystem::exists(stall_dir.path / kUpdateCheckIniFileName));
}

}  // namespace
}  // namespace goldfish::update_check

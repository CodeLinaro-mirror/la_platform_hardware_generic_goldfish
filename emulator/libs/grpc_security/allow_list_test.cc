// Copyright (C) 2023 The Android Open Source Project
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
#include "android/emulation/control/allow_list.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#include "android/base/testing/test_file_util.h"

namespace android {
namespace emulation {
namespace control {

TEST(AllowListTest, bad_is_not_null) {
    EXPECT_NE(AllowList::FromJson("xxx"), nullptr);
}

TEST(AllowListTest, detects_unprotected) {
    auto list = AllowList::FromJson(R"#(
    {
    // Set of methods that do not require any validations, the do not require a token.
    // If your method DOES NOT match this list, you will require a token of sorts.
    "unprotected": [
        ".*/getGps"
    ]})#");
    EXPECT_FALSE(list->RequiresAuthentication("foo/getGps"));
    EXPECT_TRUE(list->RequiresAuthentication("bar/huusku"));
}

TEST(AllowListTest, always_detects_unprotected) {
    auto list = AllowList::FromJson(R"#(
    {
    // Set of methods that do not require any validations, the do not require a token.
    // If your method DOES NOT match this list, you will require a token of sorts.
    "unprotected": [
        ".*/getGps"
    ]})#");

    // 2nd call might be cached.
    EXPECT_FALSE(list->RequiresAuthentication("foo/getGps"));
    EXPECT_FALSE(list->RequiresAuthentication("foo/getGps"));
    EXPECT_TRUE(list->RequiresAuthentication("bar/huusku"));
    EXPECT_TRUE(list->RequiresAuthentication("bar/huusku"));
}

TEST(AllowListTest, subject_is_protected) {
    auto list = AllowList::FromJson(R"#(
    {
    "allowlist": [
         {
            "iss": "android-studio",
            "allowed": [
                 ".*/getGps"
            ]
        }
    ]})#");

    EXPECT_TRUE(list->IsAllowed("android-studio", "foo/getGps"));
    EXPECT_FALSE(list->IsAllowed("gradle", "foo/getGps"));
}

TEST(AllowListTest, do_not_consume_memory) {
    using namespace std::chrono_literals;
    auto list = AllowList::FromJson(R"#(
    {
    "allowlist": [
         {
            "iss": "android-studio",
            "allowed": [
                 ".*/getGps"
            ]
        }
    ]})#");
    auto end = std::chrono::system_clock::now() + 1s;
    int i = 0;
    while (std::chrono::system_clock::now() < end) {
        i = (i + 1) % 512;
        EXPECT_TRUE(list->IsAllowed("android-studio", "foo" + std::to_string(i) + "/getGps"));
    }
}

TEST(AllowListTest, subject_is_always_protected) {
    auto list = AllowList::FromJson(R"#(
    {
    "allowlist": [
         {
            "iss": "android-studio",
            "allowed": [
                 ".*/getGps"
            ]
        }
    ]})#");

    EXPECT_TRUE(list->IsAllowed("android-studio", "foo/getGps"));
    EXPECT_TRUE(list->IsAllowed("android-studio", "foo/getGps"));
    EXPECT_FALSE(list->IsAllowed("gradle", "foo/getGps"));
    EXPECT_FALSE(list->IsAllowed("gradle", "foo/getGps"));
}

TEST(AllowListTest, ignore_reserved_iss) {
    auto list = AllowList::FromJson(R"#(
    {
    "allowlist": [
         {
            "iss": "__default__",
            "allowed": [
                 ".*/getGps"
            ]
        }
    ]})#");
    EXPECT_TRUE(list->RequiresAuthentication("foo/getGps"));
    EXPECT_TRUE(list->IsRed("__everyone__", "foo/getGps"));
}

TEST(AllowListTest, can_parse_default_list) {
    std::filesystem::path emu_access = std::filesystem::path(
            "goldfish+/emulator/libs/grpc_security/data/emulator_access.json");
    auto path = android::base::internal::RunfilesPath(emu_access);

    auto file = std::ifstream(path);
    ASSERT_TRUE(file.good());

    auto list = AllowList::FromStream(file);
    std::vector<std::string> allowed{
        "/android.emulation.control.EmulatorController/"
        "closeExtendedControls",
        "/android.emulation.control.EmulatorController/getClipboard",
        "/android.emulation.control.EmulatorController/"
        "getDisplayConfigurations",
        "/android.emulation.control.EmulatorController/getDisplayMode",
        "/android.emulation.control.EmulatorController/getPhysicalModel",
        "/android.emulation.control.EmulatorController/getScreenshot",
        "/android.emulation.control.EmulatorController/getStatus",
        "/android.emulation.control.EmulatorController/getVmState",
        "/android.emulation.control.EmulatorController/injectAudio",
        "/android.emulation.control.EmulatorController/"
        "rotateVirtualSceneCamera",
        "/android.emulation.control.EmulatorController/sendKey",
        "/android.emulation.control.EmulatorController/sendMouse",
        "/android.emulation.control.EmulatorController/sendTouch",
        "/android.emulation.control.EmulatorController/setClipboard",
        "/android.emulation.control.EmulatorController/"
        "setDisplayConfigurations",
        "/android.emulation.control.EmulatorController/setDisplayMode",
        "/android.emulation.control.EmulatorController/setPhysicalModel",
        "/android.emulation.control.EmulatorController/"
        "setVirtualSceneCameraVelocity",
        "/android.emulation.control.EmulatorController/setVmState",
        "/android.emulation.control.EmulatorController/"
        "showExtendedControls",
        "/android.emulation.control.EmulatorController/streamClipboard",
        "/android.emulation.control.EmulatorController/streamNotification",
        "/android.emulation.control.EmulatorController/streamScreenshot",
        "/android.emulation.control.SnapshotService/DeleteSnapshot",
        "/android.emulation.control.SnapshotService/ListSnapshots",
        "/android.emulation.control.SnapshotService/LoadSnapshot",
        "/android.emulation.control.SnapshotService/PushSnapshot",
        "/android.emulation.control.SnapshotService/SaveSnapshot"};
    for (const auto& is_green : allowed) {
        EXPECT_TRUE(list->IsAllowed("android-studio", is_green));
    }

    // Unprotected endpoints in default emulator_access.json
    EXPECT_FALSE(list->RequiresAuthentication("/grpc.health.v1.Health/Check"));
    EXPECT_FALSE(list->RequiresAuthentication(
            "/android.emulation.v2.CapabilitiesService/GetDeviceCapabilities"));
    EXPECT_TRUE(list->RequiresAuthentication("/android.emulation.v2.SystemService/RebootDevice"));

    // Studio V1 issuer must not match V2 endpoints (StaticTokenAuth is V1-only).
    EXPECT_TRUE(
            list->IsRed("android-studio", "/android.emulation.v2.SystemService/GetSystemState"));
    EXPECT_TRUE(list->IsRed("android-studio", "/android.emulation.v2.RtcService/RequestRtcStream"));

    // Gradle V1 allowed and protected endpoints must remain intact.
    EXPECT_TRUE(list->IsAllowed("gradle-utp-emulator-control",
                                "/android.emulation.control.EmulatorController/getSensor"));
    EXPECT_TRUE(list->IsProtected("gradle-utp-emulator-control",
                                  "/android.emulation.control.EmulatorController/getScreenshot"));

    // V2 Role-Based Access Control (RBAC) mappings in default emulator_access.json.
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.read",
                                    "/android.emulation.v2.DisplayService/GetScreenshot"));
    EXPECT_TRUE(
            list->IsRoleAllowed("aemu.role.read", "/google.longrunning.Operations/GetOperation"));
    EXPECT_FALSE(list->IsRoleAllowed("aemu.role.read",
                                     "/google.longrunning.Operations/CancelOperation"));
    // RtcService belongs to controller, not read, because it can inject key events.
    EXPECT_FALSE(list->IsRoleAllowed("aemu.role.read",
                                     "/android.emulation.v2.RtcService/RequestRtcStream"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.controller",
                                    "/android.emulation.v2.RtcService/RequestRtcStream"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.controller",
                                    "/android.emulation.v2.InputService/SendInputEvents"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.snapshot",
                                    "/android.emulation.v2.HypervisorService/CreateSnapshot"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.snapshot",
                                    "/google.longrunning.Operations/CancelOperation"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.admin",
                                    "/android.emulation.v2.SystemService/RebootDevice"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.admin",
                                    "/android.emulation.v2.HypervisorService/StopVm"));
}

TEST(AllowListTest, parses_roles_and_caches_results) {
    auto list = AllowList::FromJson(R"#(
    {
        "roles": {
            "aemu.role.read": [
                "/android.emulation.v2.SystemService/GetSystemState",
                "/android.emulation.v2.测试_🚀_é/Get.*"
            ],
            "aemu.role.admin": [
                "/android.emulation.v2.SystemService/RebootDevice"
            ]
        }
    })#");

    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.read",
                                    "/android.emulation.v2.SystemService/GetSystemState"));
    // Second lookup hits accept cache.
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.read",
                                    "/android.emulation.v2.SystemService/GetSystemState"));
    EXPECT_TRUE(list->IsRoleAllowed("aemu.role.read", "/android.emulation.v2.测试_🚀_é/GetState"));

    EXPECT_FALSE(list->IsRoleAllowed("aemu.role.read",
                                     "/android.emulation.v2.SystemService/RebootDevice"));
    // Second lookup hits rejection cache.
    EXPECT_FALSE(list->IsRoleAllowed("aemu.role.read",
                                     "/android.emulation.v2.SystemService/RebootDevice"));
    EXPECT_FALSE(list->IsRoleAllowed("aemu.role.unknown",
                                     "/android.emulation.v2.SystemService/GetSystemState"));

    std::vector<std::string> combined_roles = {"aemu.role.read", "aemu.role.admin"};
    EXPECT_TRUE(list->HasRolePermission(combined_roles,
                                        "/android.emulation.v2.SystemService/RebootDevice"));
}

TEST(AllowListTest, handles_malformed_roles_entries_gracefully) {
    auto list = AllowList::FromJson(R"#(
    {
        "roles": {
            "bad_role": "not_an_array",
            "good_role": ["/android.emulation.v2.InputService/.*"]
        }
    })#");
    EXPECT_FALSE(list->IsRoleAllowed("bad_role", "/android.emulation.v2.InputService/Send"));
    EXPECT_TRUE(list->IsRoleAllowed("good_role", "/android.emulation.v2.InputService/Send"));
}

TEST(AllowListTest, rejects_duplicate_role_entries_in_json) {
    EXPECT_DEATH((void)AllowList::FromJson(R"#(
    {
        "unprotected": [
            ".*/getGps"
        ],
        "roles": {
            "aemu.role.read": [
                "/android.emulation.v2.SystemService/GetSystemState"
            ],
            "aemu.role.admin": [
                "/android.emulation.v2.SystemService/StopVm"
            ],
            "aemu.role.read": [
                "/android.emulation.v2.SystemService/RebootDevice"
            ]
        }
    })#"),
                 "duplicate JSON key 'aemu.role.read'");
}

TEST(AllowListTest, rejects_duplicate_roles_in_stream_and_top_level_keys) {
    EXPECT_DEATH(
            {
                std::istringstream stream(R"#(
    {
        "roles": {
            "aemu.role.测试_🚀": [
                "/android.emulation.v2.DisplayService/GetScreenshot"
            ],
            "aemu.role.测试_🚀": [
                "/android.emulation.v2.SystemService/RebootDevice"
            ]
        }
    })#");
                (void)AllowList::FromStream(stream);
            },
            "duplicate JSON key");

    EXPECT_DEATH((void)AllowList::FromJson(R"#(
    {
        "roles": {
            "aemu.role.read": [
                "/android.emulation.v2.SystemService/GetSystemState"
            ]
        },
        "roles": {
            "aemu.role.admin": [
                "/android.emulation.v2.SystemService/RebootDevice"
            ]
        }
    })#"),
                 "duplicate JSON key 'roles'");
}
}  // namespace control
}  // namespace emulation
}  // namespace android

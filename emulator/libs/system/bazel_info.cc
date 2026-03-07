// Copyright (C) 2024 The Android Open Source Project
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
#include "android/base/bazel_info.h"

#include <iostream>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "android/base/system.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace android::base {

using ::bazel::tools::cpp::runfiles::Runfiles;

namespace {
std::vector<std::string> g_argv;
}  // namespace

bool Bazel::s_not_in_bazel{false};

std::string Bazel::RunfilesPath(const std::string& path) {
    std::string error;
    const char* workspace_dir = getenv("TEST_WORKSPACE");
    if (workspace_dir == nullptr || workspace_dir[0] == '\0') {
        std::string arg0 = g_argv.empty() ? "" : g_argv[0];
        std::unique_ptr<Runfiles> runfiles(
                Runfiles::Create(arg0, BAZEL_CURRENT_REPOSITORY, &error));
        if (!runfiles) {
            LOG(ERROR) << "Unable to create runfiles, while running under bazel: " << error;
            return "<unknown-root>";
        }
        return runfiles->Rlocation(path);
    }
    std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));

    if (runfiles == nullptr) {
        return "<create-for-test-failed>";
    }
    // 2 possibilities, it is under our workspace, or not.
    std::string location = runfiles->Rlocation(path);
    if (fs::exists(fs::path(location))) {
        return location;
    }

    return runfiles->Rlocation(absl::StrCat(workspace_dir, "/", path));
}

void Bazel::SetNotInBazel() {
    s_not_in_bazel = true;
}

bool Bazel::InBazel() {
    if (s_not_in_bazel) {
        return false;
    }
    std::array<std::string, 3> markers = {
        "BUILD_WORKING_DIRECTORY",
        "TEST_BINARY",
        "RUNFILES_DIR",
    };
    for (const auto& marker : markers) {
        if (!System::Get()->GetEnvironmentVariable(marker).empty()) {
            return true;
        }
    }
    return false;
}

void Bazel::StoreCommandLineArgs(int argc, char** argv) {
    g_argv.clear();
    for (int i = 0; i < argc; ++i) {
        g_argv.push_back(argv[i]);
    }
}

}  // namespace android::base

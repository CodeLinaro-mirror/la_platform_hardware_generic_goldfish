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
#include "android/base/bazel/bazel_info.h"

#include <iostream>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "android/base/system/System.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace android {
namespace base {

using ::bazel::tools::cpp::runfiles::Runfiles;

std::vector<std::string> g_argv;

std::string Bazel::runfilesPath(const std::string &path) {
  std::string error;
  const char *workspace_dir = getenv("TEST_WORKSPACE");
  if (workspace_dir == nullptr || workspace_dir[0] == '\0') {
    std::string arg0 = g_argv.empty() ? "" : g_argv[0];
    std::unique_ptr<Runfiles> runfiles(
        Runfiles::Create(arg0, BAZEL_CURRENT_REPOSITORY, &error));
    if (!runfiles) {
      LOG(FATAL) << "Unable to create runfiles: " << error;
    }
    return runfiles->Rlocation(path);
  } else {
    std::unique_ptr<Runfiles> runfiles(
        Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
    return runfiles->Rlocation(absl::StrCat(workspace_dir, "/", path));
  }
}

bool Bazel::inBazel() {
  std::array<std::string, 3> markers = {
      "BUILD_WORKING_DIRECTORY",
      "TEST_BINARY",
      "RUNFILES_DIR",
  };
  for (const auto &marker : markers) {
    if (!System::get()->getEnvironmentVariable(marker).empty()) {
      return true;
    }
  }
  return false;
}

void Bazel::storeCommandLineArgs(int argc, char **argv) {
  g_argv.clear();
  for (int i = 0; i < argc; ++i) {
    g_argv.push_back(argv[i]);
  }
}
} // namespace base
} // namespace android

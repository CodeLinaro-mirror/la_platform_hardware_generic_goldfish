// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "android/base/system/System.h"
#include "android/base/testing/test_file_util.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace android {
namespace base {
namespace internal {

using ::bazel::tools::cpp::runfiles::Runfiles;
namespace fs = std::filesystem;

fs::path runfilesPath(fs::path path) {
    std::string error;
    std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
    if (runfiles == nullptr) {
        LOG(FATAL) << "Unable to determine runfile path: " << error;
    }

    auto workspace_dir = System::get()->getEnvironmentVariable("TEST_WORKSPACE");
    if (workspace_dir.empty()) {
        LOG(FATAL) << "Unable to determine workspace name.";
    }

    return fs::path(runfiles->Rlocation(absl::StrCat(workspace_dir, "/", path.string())));
}

}  // namespace internal
}  // namespace base
}  // namespace android

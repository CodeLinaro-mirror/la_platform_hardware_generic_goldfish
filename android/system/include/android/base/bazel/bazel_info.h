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
#include <string>
namespace android {
namespace base {

class Bazel {
  public:
    // Returns the path of the specified file in the runfiles directory.
    static std::string runfilesPath(const std::string& path);

    // Store the command line arguments, this is needed to get the runfiles path.
    static void storeCommandLineArgs(int argc, char** argv);

    // Returns true if this executable is running in a bazel environment
    static bool inBazel();

    static void setNotInBazel();

    static bool s_notInBazel;
};
}  // namespace base
}  // namespace android

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

#include <dlfcn.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "android/base/bazel_info.h"

namespace goldfish::videobridge {
namespace {

TEST(VideobridgeCodecsDylibTest, DylibLoadsWithoutUndefinedSymbols) {
    std::string dylib_rpath = "goldfish+/emulator/videobridge/libvideobridge_codecs_shared.dylib";
    std::string full_path = android::base::Bazel::RunfilesPath(dylib_rpath);

    ASSERT_TRUE(std::filesystem::exists(full_path))
            << "Dylib not found at runfiles path: " << full_path;

    // RTLD_NOW forces dyld to resolve all undefined symbols immediately on load.
    void* handle = ::dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    const char* err = ::dlerror();
    ASSERT_NE(handle, nullptr) << "Failed to dlopen " << full_path << ": "
                               << (err != nullptr ? err : "unknown error");

    if (handle != nullptr) {
        ::dlclose(handle);
    }
}

}  // namespace
}  // namespace goldfish::videobridge

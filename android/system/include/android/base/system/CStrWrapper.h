// Copyright 2020 The Android Open Source Project
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
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace android::base {

// Helper to get a null-terminated const char* from a string_view.
// Only allocates if the string_view is not null terminated.
//
// Usage:
//
//      std::string_view myString = ...;
//      printf("Contents: %s\n", CStrWrapper(myString).c_str());
//
// c_str(...) constructs a temporary object that may allocate memory if the
// StringView is not null termianted.  The lifetime of the temporary object will
// be until the next sequence point (typically the next semicolon).  If the
// value needs to exist for longer than that, cache the instance.
//
//      std::string_view myString = ...;
//      auto myNullTerminatedString = CStrWrapper(myString).c_str();
//      functionAcceptingConstCharPointer(myNullTerminatedString);
//
class CStrWrapper {
public:
  CStrWrapper(std::string_view stringView) : mStringView(stringView) {}

  // Returns a null-terminated char*, potentially creating a copy to add a
  // null terminator.
  const char *get() {
    if (mStringView.back() == '\0') {
      return mStringView.data();
    } else {
      // Create the std::string copy on-demand.
      if (!mStringCopy) {
        mStringCopy.emplace(mStringView);
      }

      return mStringCopy->c_str();
    }
  }

  // Alias for get
  const char *c_str() { return get(); }

  // Enable casting to const char*
  operator const char *() { return get(); }

private:
  const std::string_view mStringView;
  std::optional<std::string> mStringCopy;
};

inline CStrWrapper c_str(std::string_view stringView) {
  return CStrWrapper(stringView);
}
} // namespace android::base

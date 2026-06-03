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

#pragma once

#include <string>

namespace android {

// Returns the current window manager. For Mac and Windows, it just returns
// the platform name (Mac/Windows), for linux it queries the installed window
// manager for the name
std::string getWindowManagerName();

// Returns the current desktop environment. For Mac and Windows, it just returns
// the platform name (Mac/Windows), for linux it inspects the common environment
// variables and makes the best guess based on that.
std::string getDesktopEnvironmentName();

}  // namespace android

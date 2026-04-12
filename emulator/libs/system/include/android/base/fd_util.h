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

namespace android::base {

// Mark all file descriptors (except stdin, stdout, stderr) as close-on-exec.
// On Windows, this sets the HANDLE_FLAG_INHERIT to 0 for all CRT file descriptors.
void SetAllFdsCloexec();

}  // namespace android::base

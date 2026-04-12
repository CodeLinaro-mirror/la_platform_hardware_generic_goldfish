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

#include "android/base/fd_util.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace android::base {

void SetAllFdsCloexec() {
    // We could use _getmaxstdio() or getdtablesize() but for now it is sufficient to limit to 100.
    for (int fd = 3; fd < 100; ++fd) {
#if defined(_WIN32)
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h != (HANDLE)-1 && h != NULL) {
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        }
#else
        int flags = ::fcntl(fd, F_GETFD);
        if (flags != -1) {
            ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
#endif
    }
}

}  // namespace android::base

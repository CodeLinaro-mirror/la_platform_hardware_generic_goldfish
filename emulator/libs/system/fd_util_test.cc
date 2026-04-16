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

#include <fcntl.h>
#include <gtest/gtest.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#include <windows.h>
#endif

namespace android::base {

TEST(FdUtil, set_all_fds_cloexec) {
#ifndef _WIN32
    int fds[2];
    ASSERT_EQ(0, pipe(fds));

    // Explicitly clear CLOEXEC to ensure we are testing our utility.
    fcntl(fds[0], F_SETFD, 0);
    fcntl(fds[1], F_SETFD, 0);

    ASSERT_FALSE(fcntl(fds[0], F_GETFD) & FD_CLOEXEC);
    ASSERT_FALSE(fcntl(fds[1], F_GETFD) & FD_CLOEXEC);

    SetAllFdsCloexec();

    EXPECT_TRUE(fcntl(fds[0], F_GETFD) & FD_CLOEXEC);
    EXPECT_TRUE(fcntl(fds[1], F_GETFD) & FD_CLOEXEC);

    close(fds[0]);
    close(fds[1]);
#else
    int fd = _open("NUL", 0);  // _O_RDONLY is 0
    ASSERT_NE(-1, fd);

    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    // Ensure it is inheritable first.
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    DWORD flags;
    ASSERT_TRUE(GetHandleInformation(h, &flags));
    ASSERT_TRUE(flags & HANDLE_FLAG_INHERIT);

    // Disable test handler as the function can trigger SEH.
    _invalid_parameter_handler oldHandler = _set_thread_local_invalid_parameter_handler(
            [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {});
    SetAllFdsCloexec();
    _set_thread_local_invalid_parameter_handler(oldHandler);

    ASSERT_TRUE(GetHandleInformation(h, &flags));
    EXPECT_FALSE(flags & HANDLE_FLAG_INHERIT);

    _close(fd);
#endif
}

}  // namespace android::base

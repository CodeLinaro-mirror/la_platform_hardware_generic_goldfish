// Copyright 2025 The Android Open Source Project
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

#if defined(_WIN32)
/*Because android::base::IOVector has its own definition of
 struct iovec on Windows, we need to avoid the re-definition of iovec
 from qemu/osdep.h by declaring CONFIG_IOVEC beforehand. */
#ifndef CONFIG_IOVEC
#define CONFIG_IOVEC 1
/* Structure for scatter/gather I/O.  */
struct iovec {
    void* iov_base; /* Pointer to data.  */
    size_t iov_len; /* Length of data.  */
};
#endif
#else
#include <sys/uio.h>
#endif


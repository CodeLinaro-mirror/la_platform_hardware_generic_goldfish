// Copyright 2014 The Android Open Source Project
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

#if !defined(_WIN32) && !defined(_WIN64)
#error "Only compile this file when targeting Windows!"
#endif

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

namespace android::base {

class ScopedFileHandle {
public:
    explicit ScopedFileHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedFileHandle() { close(); }
    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle(ScopedFileHandle&& other) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(ScopedFileHandle&& other) = delete;

    bool ok() const { return handle_ != INVALID_HANDLE_VALUE; }

    HANDLE get() const { return handle_; }

    HANDLE release() {
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return h;
    }

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_;
};

}  // namespace android::base

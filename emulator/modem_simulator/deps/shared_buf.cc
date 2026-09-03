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
#include "common/libs/fs/shared_buf.h"

#include <sys/types.h>

namespace cuttlefish {
namespace {
constexpr size_t kBuffSize = 4096;
}  // namespace

ssize_t ReadAll(SharedFD fd, std::string* buf) {
    buf->clear();

    ssize_t total_read = 0;
    while (true) {
        size_t current_size = buf->size();
        buf->resize(current_size + kBuffSize);

        ssize_t chunk_read = fd->Read(&(*buf)[current_size], kBuffSize);
        if (chunk_read < 0) {
            buf->resize(current_size);
            return chunk_read;
        } else if (!chunk_read) {
            return total_read;  // EOF (Read returns 0)
        }

        buf->resize(current_size + chunk_read);
        total_read += chunk_read;
    }

    return total_read;
}

ssize_t ReadExact(SharedFD fd, char* buf, size_t size) {
    size_t total_read = 0;

    while (total_read < size) {
        ssize_t read = fd->Read(&buf[total_read], size - total_read);
        if (read < 0) {
            return read;
        } else if (!read) {
            return total_read;
        }

        total_read += read;
    }

    return total_read;
}

ssize_t ReadExact(SharedFD fd, std::string* buf) {
    return ReadExact(fd, buf->data(), buf->size());
}

ssize_t WriteAll(SharedFD fd, const char* buf, size_t size) {
    size_t total_written = 0;

    while (total_written < size) {
        ssize_t written = fd->Write((void*)&(buf[total_written]), size - total_written);
        if (written <= 0) {
            if (written < 0) {
                return written;
            }
            return total_written;
        }
        total_written += written;
    }

    return total_written;
}

ssize_t WriteAll(SharedFD fd, std::string_view buf) {
    return WriteAll(fd, buf.data(), buf.size());
}

}  // namespace cuttlefish
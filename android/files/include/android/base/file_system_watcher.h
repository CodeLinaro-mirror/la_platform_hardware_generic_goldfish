// Copyright 2022` The Android Open Source Project
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

#include <stddef.h>  // for size_t

#include <cstdint>
#include <filesystem>
#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <utility>

namespace android::base {

// Listens to the file system change notifications and raises events when a
// directory, or file in a directory, changes.
//
// Note: Each observer consumes one thread.
// Note: It is very expensive on mac os, so do not observe large directory
// structures with this.
class FileSystemWatcher {
  public:
    // On one day we will have std::filesystem everywhere..
    using Path = std::filesystem::path;

    enum class WatcherChangeType : std::uint8_t {
        kCreated,  // The creation of a file or folder.
        kDeleted,  // The deletion of a file or folder.
        kChanged,  // The change of a file or folder. The types of changes
                   // include: changes to size, attributes, security
                   // settings, last write, and last access time.
    };

    // Change type, and file that was created, deleted or changed.
    using FileSystemWatcherCallback = std::function<void(WatcherChangeType, const Path&)>;

    explicit FileSystemWatcher(FileSystemWatcherCallback callback)
            : change_callback(std::move(callback)) {}
    virtual ~FileSystemWatcher() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;

    // Watches for changes in the given directory.
    // Returns nullptr if path is not a directory.
    static std::unique_ptr<FileSystemWatcher> GetFileSystemWatcher(
            const Path& path, const FileSystemWatcherCallback& on_change_callback);

    FileSystemWatcherCallback change_callback;
};
}  // namespace android::base

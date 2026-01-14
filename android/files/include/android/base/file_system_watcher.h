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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>

namespace android::base {

/**
 * @brief Listens to file system change notifications and raises events when a
 * directory, or a file in a directory, changes.
 *
 * This class provides a platform-independent interface for monitoring file
 * system activity within a specified directory.
 *
 * @note Each FileSystemWatcher instance consumes one dedicated thread to
 *       monitor for changes.
 *
 * @warning On macOS, this implementation uses the FSEvents API, which can be
 *          resource-intensive. Avoid watching large directory structures to
 *          prevent performance degradation.
 */
class FileSystemWatcher {
  public:
    // On one day we will have std::filesystem everywhere..
    using Path = std::filesystem::path;

    /**
     * @brief Defines the type of file system change that occurred.
     */
    enum class WatcherChangeType : std::uint8_t {
        kCreated,  ///< An item was created.
        kDeleted,  ///< An item was deleted.
        kChanged,  ///< An item's metadata or content was changed. This can
                   ///< include changes to size, attributes, security settings,
                   //< last write time, etc.
    };

    // Change type, and file that was created, deleted or changed.
    using FileSystemWatcherCallback = std::function<void(WatcherChangeType, const Path&)>;

    explicit FileSystemWatcher(FileSystemWatcherCallback callback)
            : change_callback(std::move(callback)) {}
    virtual ~FileSystemWatcher() = default;

    /**
     * @brief Starts the file system watcher.
     * @return true if the watcher started successfully, false otherwise.
     */
    virtual bool Start() = 0;

    /**
     * @brief Stops the file system watcher.
     */
    virtual void Stop() = 0;

    /**
     * @brief Creates a platform-specific FileSystemWatcher.
     *
     * @param path The directory to watch for changes.
     * @param on_change_callback The callback to invoke when changes are
     *                           detected.
     * @return A unique_ptr to a FileSystemWatcher instance, or nullptr if the
     *         given path is not a directory.
     */
    static std::unique_ptr<FileSystemWatcher> GetFileSystemWatcher(
            const Path& path, const FileSystemWatcherCallback& on_change_callback);

    FileSystemWatcherCallback change_callback;
};
}  // namespace android::base

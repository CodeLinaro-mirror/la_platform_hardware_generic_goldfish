// Copyright 2022 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "absl/log/log.h"

#include "aemu/base/synchronization/Event.h"
#include "android/base/file/file.h"
#include "android/base/file_system_watcher.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) \
    printf("FileSystemWatcherPosix: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::base {

/**
 * @brief POSIX (Linux) implementation of the FileSystemWatcher.
 *
 * This implementation is based on the `inotify` API, which is the standard
 * Linux mechanism for monitoring file system events.
 */
class FileSystemWatcherPosix : public FileSystemWatcher {
  public:
    FileSystemWatcherPosix(Path path, FileSystemWatcherCallback on_change_callback)
            : FileSystemWatcher(std::move(on_change_callback)), path_(std::move(path)) {}

    ~FileSystemWatcherPosix() override { Stop(); }

    bool Start() override {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return false;
        }
        std::thread watcher([this] { WatchForChanges(); });
        watcher_thread_ = std::move(watcher);
        started_.wait();
        return notify_fd_ != 0 && pipe_[0] != -1;
    }

    void Stop() override {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false)) {
            DD("Closing watchers");
            if (pipe_[1] != -1) {
                write(pipe_[1], "x", 1);
            }
            watcher_thread_.join();
        }
    }

  private:
    void WaitForFdEvents() {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipe_[0], &readfds);
        FD_SET(notify_fd_, &readfds);
        select(std::max(pipe_[0].load(), notify_fd_) + 1, &readfds, nullptr, nullptr, nullptr);
    }

    bool WatchForChanges() {
        notify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (notify_fd_ < 1) {
            notify_fd_ = 0;
            started_.signal();
            return false;
        }

        int p[2];
        if (pipe(p) != 0 || (fcntl(p[0], F_SETFL, O_NONBLOCK) < 0)) {
            PLOG(ERROR) << "Unable to open pipe.";
            started_.signal();
            return false;
        };
        pipe_[0] = p[0];
        pipe_[1] = p[1];

        // Note, this will not work for filenames longer than 256 chars, this
        // does not include the path.
        constexpr int kMaxEvents = 4;
        constexpr int kEventSize = sizeof(struct inotify_event);
        constexpr int kMaxFilename = 256;
        constexpr int kEventBuffer = kMaxEvents * (kEventSize + kMaxFilename);

        auto fd = inotify_add_watch(notify_fd_, path_.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE);

        if (fd == -1) {
            close(notify_fd_);
            notify_fd_ = 0;
            started_.signal();
            return false;
        }

        started_.signal();
        while (running_) {
            uint8_t buffer[kEventBuffer];

            WaitForFdEvents();

            if (!running_) {
                break;
            }

            const ssize_t length = read(notify_fd_, buffer, sizeof(buffer));
            DD("Read %zd bytes", length);

            ssize_t i = 0;
            while (i < length && running_) {
                auto* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);
                DD("i: %zd, event->len: %d", i, event->len);
                if (event->len) {
                    const Path changed = path_ / event->name;
                    DD("Changed: %s", changed.c_str());
                    if (event->mask & IN_CREATE) {
                        change_callback(WatcherChangeType::kCreated, changed);
                    } else if (event->mask & IN_DELETE) {
                        change_callback(WatcherChangeType::kDeleted, changed);
                    } else if (event->mask & IN_MODIFY) {
                        change_callback(WatcherChangeType::kChanged, changed);
                    }
                }
                i += kEventSize + event->len;
            }
        }

        DD("Exit loop");
        close(notify_fd_);
        if (const int fd = pipe_[0] != -1) {
            pipe_[0] = -1;
            close(fd);
        }
        if (const int fd = pipe_[1] != -1) {
            pipe_[1] = -1;
            close(fd);
        }
        return true;
    }

    Path path_;
    std::atomic_bool running_{false};
    std::thread watcher_thread_;
    Event started_;
    int notify_fd_{0};
    std::atomic_int pipe_[2] = {-1, -1};
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const Path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherPosix>(path, on_change_callback);
};
}  // namespace android::base

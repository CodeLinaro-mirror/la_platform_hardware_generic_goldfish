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
#include <poll.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "android/base/file_system_watcher.h"
#include "goldfish/file/file.h"

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
    enum class State : std::uint8_t {
        kIdle,
        kStarting,
        kRunning,
        kStopping,
    };

    FileSystemWatcherPosix(std::filesystem::path path, FileSystemWatcherCallback on_change_callback)
            : FileSystemWatcher(std::move(on_change_callback)), path_(std::move(path)) {}

    ~FileSystemWatcherPosix() override { Stop(); }

    bool Start() override {
        {
            const absl::MutexLock lock(mu_);
            if (state_ != State::kIdle) {
                return false;
            }
            state_ = State::kStarting;
            worker_done_ = false;
            DCHECK(!watcher_thread_.joinable()) << "A watcher thread is active.";
            watcher_thread_ = std::thread([this] { WatchForChanges(); });
            mu_.Await(absl::Condition(this, &FileSystemWatcherPosix::IsNotStarting));
            if (state_ == State::kRunning) {
                return true;
            }
        }
        Stop();
        return false;
    }

    void Stop() override {
        std::thread thread_to_join;
        {
            const absl::MutexLock lock(mu_);
            if (state_ == State::kIdle) {
                DCHECK(!watcher_thread_.joinable());
                return;
            }
            if (state_ == State::kStopping) {
                mu_.Await(absl::Condition(this, &FileSystemWatcherPosix::IsIdle));
                return;
            }
            state_ = State::kStopping;
            DD("Closing watchers");
            const int wake_fd = pipe_[1].load();
            if (wake_fd != -1) {
                (void)write(wake_fd, "x", 1);
            }
            thread_to_join = std::move(watcher_thread_);
        }

        if (thread_to_join.joinable()) {
            thread_to_join.join();
        }

        {
            const absl::MutexLock lock(mu_);
            state_ = State::kIdle;
        }
    }

  private:
    bool IsNotStarting() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
        return state_ != State::kStarting || worker_done_;
    }

    bool IsIdle() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return state_ == State::kIdle; }

    bool IsRunning() {
        const absl::MutexLock lock(mu_);
        return state_ == State::kRunning;
    }

    void WaitForFdEvents() {
        struct pollfd fds[2] = {
            {.fd = pipe_[0].load(), .events = POLLIN, .revents = 0},
            {.fd = notify_fd_, .events = POLLIN, .revents = 0},
        };
        (void)poll(fds, 2, -1);
    }

    bool WatchForChanges() {
        const absl::Cleanup cleanup_worker = [this] {
            if (notify_fd_ >= 0) {
                close(notify_fd_);
                notify_fd_ = -1;
            }
            const int rfd = pipe_[0].exchange(-1);
            if (rfd != -1) {
                close(rfd);
            }
            const absl::MutexLock lock(mu_);
            const int wfd = pipe_[1].exchange(-1);
            if (wfd != -1) {
                close(wfd);
            }
            worker_done_ = true;
        };

        notify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (notify_fd_ < 0) {
            notify_fd_ = -1;
            return false;
        }

        int p[2];
        if (pipe2(p, O_NONBLOCK | O_CLOEXEC) != 0) {
            PLOG(ERROR) << "Unable to open pipe.";
            return false;
        }
        pipe_[0] = p[0];
        {
            const absl::MutexLock lock(mu_);
            pipe_[1] = p[1];
        }

        // Note, this will not work for filenames longer than 256 chars, this
        // does not include the path.
        constexpr size_t kMaxEvents = 4;
        constexpr size_t kEventSize = sizeof(struct inotify_event);
        constexpr size_t kMaxFilename = 256;
        constexpr size_t kEventBuffer = kMaxEvents * (kEventSize + kMaxFilename);

        auto fd =
                inotify_add_watch(notify_fd_, path_.c_str(),
                                  IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);

        if (fd == -1) {
            return false;
        }

        {
            const absl::MutexLock lock(mu_);
            if (state_ == State::kStopping) {
                return false;
            }
            state_ = State::kRunning;
        }

        while (IsRunning()) {
            alignas(struct inotify_event) uint8_t buffer[kEventBuffer];

            WaitForFdEvents();

            if (!IsRunning()) {
                break;
            }

            const ssize_t length = read(notify_fd_, buffer, sizeof(buffer));
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                PLOG(ERROR) << "Failed to read inotify events.";
                break;
            }
            if (length == 0) {
                break;
            }
            DD("Read %zd bytes", length);

            const auto total_bytes = static_cast<size_t>(length);
            size_t i = 0;
            while (i + kEventSize <= total_bytes && IsRunning()) {
                auto* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);
                if (i + kEventSize + event->len > total_bytes) {
                    break;
                }
                DD("i: %zu, event->len: %u", i, event->len);
                if (event->len) {
                    const std::filesystem::path changed = path_ / event->name;
                    DD("Changed: %s", changed.c_str());
                    if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                        change_callback(WatcherChangeType::kCreated, changed);
                    } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                        change_callback(WatcherChangeType::kDeleted, changed);
                    } else if (event->mask & IN_MODIFY) {
                        change_callback(WatcherChangeType::kChanged, changed);
                    }
                }
                i += kEventSize + event->len;
            }
        }

        DD("Exit loop");
        return true;
    }

    std::filesystem::path path_;
    absl::Mutex mu_;
    State state_ ABSL_GUARDED_BY(mu_) = State::kIdle;
    bool worker_done_ ABSL_GUARDED_BY(mu_) = false;
    std::thread watcher_thread_ ABSL_GUARDED_BY(mu_);
    int notify_fd_{-1};
    std::atomic_int pipe_[2] = {-1, -1};
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const std::filesystem::path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherPosix>(path, on_change_callback);
};
}  // namespace android::base

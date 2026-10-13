// Copyright (C) 2025 The Android Open Source Project
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
#include "android/base/file_system_watcher_darwin.h"

#include <CoreServices/CoreServices.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"

#include "android/base/file_system_watcher.h"
#include "goldfish/base/unique_handle.h"
#include "goldfish/file/file.h"

namespace android::base {

namespace {
// Debug helpers, this allows us to log the file flags in a human readable form.
struct FSEventFlagsWrapper {
    FSEventStreamEventFlags flags;
};

struct CFTypeDeleter {
    struct Empty {};
    CFTypeDeleter() = default;
    explicit CFTypeDeleter(Empty) {}
    void operator()(CFTypeRef ref) const {
        if (ref) CFRelease(ref);
    }
};

template <typename T>
using UniqueCFType = goldfish::base::UniqueHandle<T, nullptr, CFTypeDeleter>;

bool IsCreatedEvent(FSEventStreamEventFlags flags) {
    return flags & kFSEventStreamEventFlagItemCreated;
}

bool IsRemovedEvent(FSEventStreamEventFlags flags) {
    return flags & kFSEventStreamEventFlagItemRemoved;
}

bool IsRenamedEvent(FSEventStreamEventFlags flags) {
    return flags & kFSEventStreamEventFlagItemRenamed;
}

bool IsModifiedEvent(FSEventStreamEventFlags flags) {
    return (flags & kFSEventStreamEventFlagItemModified ||
            flags & kFSEventStreamEventFlagItemInodeMetaMod ||
            flags & kFSEventStreamEventFlagItemXattrMod) &&
           !IsRemovedEvent(flags);
}

std::string StripTrailingSlashes(std::string path_str) {
    while (path_str.size() > 1 && path_str.back() == '/') {
        path_str.pop_back();
    }
    return path_str;
}

template <typename Sink>
void AppendFlag(Sink& sink, bool* first, absl::string_view flag) {
    if (!(*first)) {
        sink.Append("|");
    }
    sink.Append(flag);
    *first = false;
}

template <typename Sink>
void AbslStringify(Sink& sink, FSEventFlagsWrapper wrapper) {
    const FSEventStreamEventFlags flags = wrapper.flags;
    if (flags == kFSEventStreamEventFlagNone) {
        sink.Append("None");
        return;
    }

    bool first = true;

    // --- Item-level flags ---
    if (flags & kFSEventStreamEventFlagItemCreated) {
        AppendFlag(sink, &first, "Created");
    }
    if (flags & kFSEventStreamEventFlagItemRemoved) {
        AppendFlag(sink, &first, "Removed");
    }
    if (flags & kFSEventStreamEventFlagItemModified) {
        AppendFlag(sink, &first, "Modified");
    }
    if (flags & kFSEventStreamEventFlagItemInodeMetaMod) {
        AppendFlag(sink, &first, "InodeMetaMod");
    }
    if (flags & kFSEventStreamEventFlagItemRenamed) {
        AppendFlag(sink, &first, "Renamed");
    }
    if (flags & kFSEventStreamEventFlagItemXattrMod) {
        AppendFlag(sink, &first, "XattrMod");
    }
    if (flags & kFSEventStreamEventFlagItemFinderInfoMod) {
        AppendFlag(sink, &first, "FinderInfoMod");
    }
    if (flags & kFSEventStreamEventFlagItemChangeOwner) {
        AppendFlag(sink, &first, "ChangeOwner");
    }
    if (flags & kFSEventStreamEventFlagItemCloned) {
        AppendFlag(sink, &first, "Cloned");
    }

    // --- Item type flags ---
    if (flags & kFSEventStreamEventFlagItemIsFile) {
        AppendFlag(sink, &first, "IsFile");
    }
    if (flags & kFSEventStreamEventFlagItemIsDir) {
        AppendFlag(sink, &first, "IsDir");
    }
    if (flags & kFSEventStreamEventFlagItemIsSymlink) {
        AppendFlag(sink, &first, "IsSymlink");
    }
    if (flags & kFSEventStreamEventFlagItemIsHardlink) {
        AppendFlag(sink, &first, "IsHardlink");
    }

    // --- Stream-level flags ---
    if (flags & kFSEventStreamEventFlagMustScanSubDirs) {
        AppendFlag(sink, &first, "MustScanSubDirs");
    }
    if (flags & kFSEventStreamEventFlagUserDropped) {
        AppendFlag(sink, &first, "UserDropped");
    }
    if (flags & kFSEventStreamEventFlagKernelDropped) {
        AppendFlag(sink, &first, "KernelDropped");
    }
    if (flags & kFSEventStreamEventFlagEventIdsWrapped) {
        AppendFlag(sink, &first, "EventIdsWrapped");
    }
    if (flags & kFSEventStreamEventFlagHistoryDone) {
        AppendFlag(sink, &first, "HistoryDone");
    }
    if (flags & kFSEventStreamEventFlagRootChanged) {
        AppendFlag(sink, &first, "RootChanged");
    }
    if (flags & kFSEventStreamEventFlagMount) {
        AppendFlag(sink, &first, "Mount");
    }
    if (flags & kFSEventStreamEventFlagUnmount) {
        AppendFlag(sink, &first, "Unmount");
    }
}
}  // namespace

// Filesystem watcher based on
// https://developer.apple.com/documentation/coreservices/file_system_events
// api.
class FileSystemWatcherFS : public FileSystemWatcher {
  public:
    enum class State : std::uint8_t {
        kIdle,      ///< No thread is running. Initial and final state.
        kStarting,  ///< Start() was called, thread is spawning and setting up resources.
        kRunning,   ///< Background thread is actively in the CFRunLoop.
        kStopping   ///< Stop() was called, thread is signaled to exit.
    };

    FileSystemWatcherFS(std::filesystem::path path, FileSystemWatcherCallback on_change_callback,
                        DarwinWatcherOptions options = {})
            : FileSystemWatcher(std::move(on_change_callback))
            , path_(std::move(path))
            , watched_dir_prefix_(StripTrailingSlashes(path_.string()))
            , canonical_dir_prefix_(ResolveCanonicalPrefix(path_))
            , options_(std::move(options)) {}

    ~FileSystemWatcherFS() override { Stop(); }

    bool Start() override {
        if (!__builtin_available(macOS 14.0, *)) {
            LOG(WARNING) << "Running on macOS < 14: FSEvents may experience performance issues "
                            "that could result in missed events on volumes with heavy churn.";
        }
        {
            const absl::MutexLock lock(mu_);
            if (state_ != State::kIdle) {
                return false;
            }

            state_ = State::kStarting;
            worker_done_ = false;
            cf_run_loop_ = nullptr;
            DCHECK(!watcher_thread_.joinable()) << "A watcher thread is active.";
            watcher_thread_ = std::thread([this] { WatchForChanges(); });

            // Wait for the thread to move out of the starting state.
            if (!mu_.AwaitWithTimeout(absl::Condition(this, &FileSystemWatcherFS::IsNotStarting),
                                      options_.startup_timeout)) {
                LOG(ERROR) << "Timed out waiting for FileSystemWatcher to start for " << path_
                           << ". You might not receive file change notifications for this "
                              "directory.";
            }
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
                mu_.Await(absl::Condition(this, &FileSystemWatcherFS::IsIdle));
                return;
            }

            state_ = State::kStopping;
            if (cf_run_loop_) {
                CFRunLoopStop(cf_run_loop_);
            }
            thread_to_join = std::move(watcher_thread_);
        }

        // Allow thread to exit without holding the lock
        if (thread_to_join.joinable()) {
            thread_to_join.join();
        }

        {
            const absl::MutexLock lock(mu_);
            cf_run_loop_ = nullptr;
            state_ = State::kIdle;
        }
    }

  private:
    static std::string ResolveCanonicalPrefix(const std::filesystem::path& path) {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return StripTrailingSlashes(ec ? path.string() : canonical.string());
    }

    std::filesystem::path NormalizeEventPath(absl::string_view event_path) const {
        if (event_path == canonical_dir_prefix_) {
            return std::filesystem::path(watched_dir_prefix_);
        }
        if (absl::StartsWith(event_path, canonical_dir_prefix_) &&
            event_path.size() > canonical_dir_prefix_.size() &&
            event_path[canonical_dir_prefix_.size()] == '/') {
            return std::filesystem::path(absl::StrCat(
                    watched_dir_prefix_, event_path.substr(canonical_dir_prefix_.size())));
        }
        return std::filesystem::path(std::string(event_path));
    }

    bool IsNotStarting() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
        return state_ != State::kStarting || worker_done_;
    }

    bool IsIdle() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return state_ == State::kIdle; }

    static void NotifyRunning(void* info) {
        auto* self = static_cast<FileSystemWatcherFS*>(info);
        const absl::MutexLock lock(self->mu_);
        self->cf_run_loop_ = CFRunLoopGetCurrent();
        if (self->state_ == State::kStopping) {
            CFRunLoopStop(self->cf_run_loop_);
            return;
        }
        VLOG(1) << "Filesystem watcher is active";
        DCHECK(self->state_ == State::kStarting)
                << "We should be in the starting state when this callback is happening";
        self->state_ = State::kRunning;
    }

    static void WatcherCb(ConstFSEventStreamRef, void* client_call_back_info, size_t num_events,
                          void* event_paths, const FSEventStreamEventFlags event_flags[],
                          const FSEventStreamEventId*) {
        auto* watcher = static_cast<FileSystemWatcherFS*>(client_call_back_info);
        {
            const absl::MutexLock lock(watcher->mu_);
            if (watcher->state_ != State::kRunning) {
                return;
            }
        }

        char** paths = static_cast<char**>(event_paths);
        for (size_t i = 0; i < num_events; i++) {
            if (!paths[i]) continue;
            const std::filesystem::path path = watcher->NormalizeEventPath(paths[i]);
            const FSEventStreamEventFlags flags = event_flags[i];

            if (IsRemovedEvent(flags) || IsRenamedEvent(flags)) {
                if (!base::file::exists(path)) {
                    watcher->change_callback(WatcherChangeType::kDeleted, path);
                    continue;
                }
                if (IsRemovedEvent(flags)) {
                    watcher->change_callback(WatcherChangeType::kDeleted, path);
                }
                if (IsCreatedEvent(flags) || IsRenamedEvent(flags)) {
                    watcher->change_callback(WatcherChangeType::kCreated, path);
                }
                if (IsModifiedEvent(flags)) {
                    watcher->change_callback(WatcherChangeType::kChanged, path);
                }
                continue;
            }

            if (IsCreatedEvent(flags)) {
                watcher->change_callback(WatcherChangeType::kCreated, path);
            }
            if (IsModifiedEvent(flags)) {
                watcher->change_callback(WatcherChangeType::kChanged, path);
            }
        }
    }

    void WatchForChanges() {
        const absl::Cleanup mark_worker_done = [this] {
            const absl::MutexLock lock(mu_);
            cf_run_loop_ = nullptr;
            worker_done_ = true;
        };

        // Setup file event stream.
        const UniqueCFType<CFStringRef> dir(
                CFStringCreateWithCString(nullptr, path_.string().c_str(), kCFStringEncodingUTF8));
        CFStringRef dir_ptr = dir.get();
        const UniqueCFType<CFArrayRef> paths_to_watch(CFArrayCreate(
                nullptr, reinterpret_cast<const void**>(&dir_ptr), 1, &kCFTypeArrayCallBacks));

        FSEventStreamContext stream_ctx = {0, this, nullptr, nullptr, nullptr};
        FSEventStreamRef raw_stream =
                options_.stream_create(nullptr, &FileSystemWatcherFS::WatcherCb, &stream_ctx,
                                       paths_to_watch.get(), kFSEventStreamEventIdSinceNow, 0,
                                       kFSEventStreamCreateFlagFileEvents  // Get file-level events
                                               | kFSEventStreamCreateFlagNoDefer);  // Get them ASAP

        if (!raw_stream) return;

        bool stream_scheduled = false;
        bool stream_started = false;
        const absl::Cleanup cleanup_stream = [&raw_stream, &stream_scheduled, &stream_started] {
            if (stream_started) {
                FSEventStreamStop(raw_stream);
            }
            if (stream_scheduled) {
                FSEventStreamInvalidate(raw_stream);
            }
            FSEventStreamRelease(raw_stream);
        };

        // Setup run loop source to signal that we are fully operational.
        {
            const absl::MutexLock lock(mu_);
            if (state_ == State::kStopping) {
                return;
            }
        }

        CFRunLoopRef run_loop = CFRunLoopGetCurrent();
        CFRunLoopSourceContext source_ctx = {0,       this,    nullptr, nullptr, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, NotifyRunning};
        UniqueCFType<CFRunLoopSourceRef> source(
                options_.run_loop_source_create(nullptr, 0, &source_ctx));
        if (!source) {
            return;
        }

        // Guaranteed to run once the runloop is active.
        CFRunLoopAddSource(run_loop, source.get(), kCFRunLoopDefaultMode);
        CFRunLoopSourceSignal(source.get());
        CFRunLoopWakeUp(run_loop);

        FSEventStreamScheduleWithRunLoop(raw_stream, run_loop, kCFRunLoopDefaultMode);
        stream_scheduled = true;
        if (!options_.stream_start(raw_stream)) {
            return;
        }
        stream_started = true;

        {
            const absl::MutexLock lock(mu_);
            if (state_ == State::kStopping) {
                return;
            }
        }

        CFRunLoopRun();  // Waits until we cancel it (by calling CFRunLoopStop).
    }

    const std::filesystem::path path_;
    const std::string watched_dir_prefix_;
    const std::string canonical_dir_prefix_;
    DarwinWatcherOptions options_;
    absl::Mutex mu_;
    State state_ ABSL_GUARDED_BY(mu_) = State::kIdle;
    bool worker_done_ ABSL_GUARDED_BY(mu_) = false;
    std::thread watcher_thread_ ABSL_GUARDED_BY(mu_);
    CFRunLoopRef cf_run_loop_ ABSL_GUARDED_BY(mu_) = nullptr;
};

std::unique_ptr<FileSystemWatcher> CreateDarwinFileSystemWatcherForTesting(
        const std::filesystem::path& path,
        FileSystemWatcher::FileSystemWatcherCallback on_change_callback,
        DarwinWatcherOptions options) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherFS>(path, std::move(on_change_callback),
                                                 std::move(options));
}

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const std::filesystem::path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherFS>(path, on_change_callback);
};
}  // namespace android::base

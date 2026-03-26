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
#include <CoreServices/CoreServices.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
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

struct FSEventStreamDeleter {
    struct Empty {};
    FSEventStreamDeleter() = default;
    explicit FSEventStreamDeleter(Empty) {}
    void operator()(FSEventStreamRef stream) const {
        if (stream) {
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
        }
    }
};

using UniqueFSEventStream =
        goldfish::base::UniqueHandle<FSEventStreamRef, nullptr, FSEventStreamDeleter>;

bool IsCreatedEvent(FSEventStreamEventFlags flags) {
    return flags & kFSEventStreamEventFlagItemCreated;
}

bool IsRemovedEvent(FSEventStreamEventFlags flags) {
    return flags & kFSEventStreamEventFlagItemRemoved;
}

bool IsModifiedEvent(FSEventStreamEventFlags flags) {
    return (flags & kFSEventStreamEventFlagItemModified ||
            flags & kFSEventStreamEventFlagItemInodeMetaMod ||
            flags & kFSEventStreamEventFlagItemXattrMod) &&
           !IsRemovedEvent(flags);
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
using Path = FileSystemWatcher::Path;

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

    FileSystemWatcherFS(Path path, FileSystemWatcherCallback on_change_callback)
            : FileSystemWatcher(std::move(on_change_callback)), path_(std::move(path)) {}

    ~FileSystemWatcherFS() override { Stop(); }

    bool Start() override {
        const absl::MutexLock lock(mu_);
        if (state_ != State::kIdle) {
            return false;
        }

        state_ = State::kStarting;
        cf_run_loop_ = nullptr;
        CHECK(!watcher_thread_.joinable()) << "A watcher thread is active.";
        watcher_thread_ = std::thread([this] { WatchForChanges(); });

        // Wait for the thread to move out of the starting state.
        if (!mu_.AwaitWithTimeout(absl::Condition(this, &FileSystemWatcherFS::IsNotStarting),
                                  absl::Seconds(5))) {
            LOG(FATAL) << "Timed out waiting for FileSystemWatcher to start for " << path_
                       << ". You might not receive file change notifications for this directory.";
        }
        return state_ == State::kRunning;
    }

    void Stop() override {
        std::thread thread_to_join;
        {
            const absl::MutexLock lock(mu_);
            if (state_ == State::kIdle || state_ == State::kStopping) {
                return;
            }

            // Signal the thread to stop.
            // Note that State::kRunning -> cf_run_loop_
            CHECK(state_ == State::kRunning) << "Start was called, yet we are not running?";
            CHECK(watcher_thread_.joinable()) << "Watcher thread is not joinable.";
            CFRunLoopStop(cf_run_loop_);
            state_ = State::kStopping;
            thread_to_join = std::move(watcher_thread_);
        }

        // Allow thread to exit without holding the lock
        if (thread_to_join.joinable()) {
            thread_to_join.join();
        }
    }

  private:
    bool IsNotStarting() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
        return state_ != State::kStarting;
    }

    static void NotifyRunning(void* info) {
        auto* self = static_cast<FileSystemWatcherFS*>(info);
        const absl::MutexLock lock(self->mu_);
        VLOG(1) << "Filesystem watcher is active";
        CHECK(self->state_ == State::kStarting)
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
            const std::string path = paths[i];
            const FSEventStreamEventFlags flags = event_flags[i];

            if (IsRemovedEvent(flags)) {
                watcher->change_callback(WatcherChangeType::kDeleted, path);
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
        const absl::Cleanup back_to_idle = [this] {
            const absl::MutexLock lock(mu_);
            cf_run_loop_ = nullptr;
            state_ = State::kIdle;
        };

        // Setup file event stream.
        const UniqueCFType<CFStringRef> dir(
                CFStringCreateWithCString(nullptr, path_.string().c_str(), kCFStringEncodingUTF8));
        CFStringRef dir_ptr = dir.get();
        const UniqueCFType<CFArrayRef> paths_to_watch(CFArrayCreate(
                nullptr, reinterpret_cast<const void**>(&dir_ptr), 1, &kCFTypeArrayCallBacks));

        FSEventStreamContext stream_ctx = {0, this, nullptr, nullptr, nullptr};
        const UniqueFSEventStream stream(
                FSEventStreamCreate(nullptr, &FileSystemWatcherFS::WatcherCb, &stream_ctx,
                                    paths_to_watch.get(), kFSEventStreamEventIdSinceNow, 0,
                                    kFSEventStreamCreateFlagFileEvents  // Get file-level events
                                            | kFSEventStreamCreateFlagNoDefer));  // Get them ASAP

        if (!stream) return;

        // Setup run loop source to signal that we are fully operational.
        {
            const absl::MutexLock lock(mu_);
            cf_run_loop_ = CFRunLoopGetCurrent();
            CFRunLoopSourceContext source_ctx = {0,       this,    nullptr, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, NotifyRunning};

            auto* source = CFRunLoopSourceCreate(nullptr, 0, &source_ctx);
            if (source) {
                // Guaranteed to run once the runloop is active.
                CFRunLoopAddSource(cf_run_loop_, source, kCFRunLoopDefaultMode);
                CFRunLoopSourceSignal(source);
                CFRunLoopWakeUp(cf_run_loop_);
                CFRelease(source);
            } else {
                // note, you might miss events between now and when CFRunLoopRun() is called.
                state_ = State::kRunning;
            }

            FSEventStreamScheduleWithRunLoop(stream.get(), cf_run_loop_, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream.get());
        }

        CFRunLoopRun();  // Waits until we cancel it (by calling CFRunLoopStop).
        FSEventStreamStop(stream.get());
    }

    const Path path_;
    absl::Mutex mu_;
    State state_ ABSL_GUARDED_BY(mu_) = State::kIdle;
    std::thread watcher_thread_ ABSL_GUARDED_BY(mu_);
    CFRunLoopRef cf_run_loop_ ABSL_GUARDED_BY(mu_) = nullptr;
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const Path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherFS>(path, on_change_callback);
};
}  // namespace android::base

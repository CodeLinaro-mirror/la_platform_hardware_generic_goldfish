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

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#include "aemu/base/synchronization/Event.h"
#include "android/base/file/file.h"
#include "android/base/file_system_watcher.h"

namespace android::base {

namespace {
// Debug helpers, this allows us to log the file flags in a human readable form.
struct FSEventFlagsWrapper {
    FSEventStreamEventFlags flags;
};

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
    FileSystemWatcherFS(Path path, FileSystemWatcherCallback on_change_callback)
            : FileSystemWatcher(std::move(on_change_callback)), path_(std::move(path)) {}

    ~FileSystemWatcherFS() override { Stop(); }

    bool Start() override {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return false;
        }
        std::thread watcher([this] { WatchForChanges(); });
        watcher_thread_ = std::move(watcher);
        started_.wait();
        return cf_run_loop_ != nullptr;
    }

    void Stop() override {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false)) {
            if (cf_run_loop_) {
                CFRunLoopStop(cf_run_loop_);
            }
            started_.signal();
            watcher_thread_.join();
        }
    }

  private:
    static void WatcherCb(ConstFSEventStreamRef, void* client_call_back_info, size_t num_events,
                          void* event_paths, const FSEventStreamEventFlags event_flags[],
                          const FSEventStreamEventId*) {
        auto* watcher = static_cast<FileSystemWatcherFS*>(client_call_back_info);
        // Only process events if the watcher is still considered running.
        if (!watcher->running_.load()) {
            return;
        }

        char** paths = static_cast<char**>(event_paths);

        for (size_t i = 0; i < num_events; i++) {
            if (!paths[i]) {
                continue;
            }
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

    bool WatchForChanges() {
        cf_run_loop_ = nullptr;
        const auto* dir =
                CFStringCreateWithCString(nullptr, path_.string().c_str(), kCFStringEncodingUTF8);
        const auto* paths_to_watch = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&dir), 1,
                                                   &kCFTypeArrayCallBacks);

        FSEventStreamContext stream_ctx = {0, this, nullptr, nullptr, nullptr};
        auto* stream =
                FSEventStreamCreate(nullptr, &FileSystemWatcherFS::WatcherCb, &stream_ctx,
                                    paths_to_watch, kFSEventStreamEventIdSinceNow, 0,
                                    kFSEventStreamCreateFlagFileEvents |  // Get file-level events
                                            kFSEventStreamCreateFlagNoDefer);  // Get them ASAP

        if (!stream) {
            started_.signal();
            return false;
        }

        cf_run_loop_ = CFRunLoopGetCurrent();
        FSEventStreamScheduleWithRunLoop(stream, cf_run_loop_, kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);

        started_.signal();

        CFRunLoopRun();  // Waits until we cancel it (by calling CFRunLoopStop).

        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);

        return true;
    }

    Path path_;
    std::atomic_bool running_{false};
    std::thread watcher_thread_;
    Event started_;
    CFRunLoopRef cf_run_loop_;
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const Path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherFS>(path, on_change_callback);
};
}  // namespace android::base

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
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#include "aemu/base/files/FileSystemWatcher.h"
#include "aemu/base/synchronization/Event.h"
#include "android/base/system/System.h"

namespace android {
namespace base {

namespace {
// Debug helpers, this allows us to log the file flags in a human readable form.
struct FSEventFlagsWrapper {
    FSEventStreamEventFlags flags;
};

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
    FSEventStreamEventFlags flags = wrapper.flags;
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
//
// Note: Modify events *only* detect timestamp and attribute changes.
class FileSystemWatcherFS : public FileSystemWatcher {
  public:
    FileSystemWatcherFS(Path path, FileSystemWatcherCallback onChangeCallback)
            : FileSystemWatcher(onChangeCallback), mPath(path) {}

    ~FileSystemWatcherFS() { stop(); }

    bool start() override {
        bool expected = false;
        if (!mRunning.compare_exchange_strong(expected, true)) {
            return false;
        }
        std::thread watcher([this] { watchForChanges(); });
        mWatcherThread = std::move(watcher);
        mStarted.wait();
        return mCfRunLoop != nullptr;
    }

    void stop() override {
        bool expected = true;
        if (mRunning.compare_exchange_strong(expected, false)) {
            VLOG(1) << "Stopping loop.";
            if (mCfRunLoop) {
                CFRunLoopStop(mCfRunLoop);
            }
            mStarted.signal();
            mWatcherThread.join();
        }
    }

  private:
    static void watcherCb(ConstFSEventStreamRef, void* clientCallBackInfo, size_t numEvents,
                          void* eventPaths, const FSEventStreamEventFlags eventFlags[],
                          const FSEventStreamEventId*) {
        auto* watcher = static_cast<FileSystemWatcherFS*>(clientCallBackInfo);
        char** paths = (char**)eventPaths;

        for (size_t i = 0; i < numEvents; i++) {
            std::string path = paths[i];
            const FSEventStreamEventFlags flags = eventFlags[i];

            VLOG(1) << "FSEvent: path=" << path << ", flags=" << FSEventFlagsWrapper{flags};

            if (flags & kFSEventStreamEventFlagItemRemoved) {
                watcher->mChangeCallback(WatcherChangeType::Deleted, path);
            } else if (flags & kFSEventStreamEventFlagItemModified &&
                       (flags & kFSEventStreamEventFlagItemInodeMetaMod ||
                        flags & kFSEventStreamEventFlagItemXattrMod)) {
                // Note, most change events are fired as Created|Modified|...
                // we only take the ones that modify the attr, and inode meta.
                watcher->mChangeCallback(WatcherChangeType::Changed, path);
            } else if (flags & kFSEventStreamEventFlagItemCreated) {
                // You might get a change event (i.e. file contents change) as a create event.
                watcher->mChangeCallback(WatcherChangeType::Created, path);
            }
        }
    }

    bool watchForChanges() {
        mCfRunLoop = nullptr;
        auto dir = CFStringCreateWithCString(nullptr, mPath.c_str(), kCFStringEncodingUTF8);
        auto pathsToWatch = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&dir), 1,
                                          &kCFTypeArrayCallBacks);

        FSEventStreamContext streamCtx = {0, this, NULL, NULL, NULL};
        auto stream =
                FSEventStreamCreate(nullptr, &FileSystemWatcherFS::watcherCb, &streamCtx,
                                    pathsToWatch, kFSEventStreamEventIdSinceNow, 0,
                                    kFSEventStreamCreateFlagFileEvents |  // Get file-level events
                                            kFSEventStreamCreateFlagNoDefer);  // Get them ASAP

        if (!stream) {
            mStarted.signal();
            return false;
        }

        mCfRunLoop = CFRunLoopGetCurrent();
        FSEventStreamScheduleWithRunLoop(stream, mCfRunLoop, kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);

        mStarted.signal();

        VLOG(1) << "Starting run loop.";
        CFRunLoopRun();  // Waits until we cancel it (by calling CFRunLoopStop).

        VLOG(1) << "Completed watcher loop.";
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);

        return true;
    }

    Path mPath;
    std::atomic_bool mRunning{false};
    std::thread mWatcherThread;
    Event mStarted;
    CFRunLoopRef mCfRunLoop;
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::getFileSystemWatcher(
        Path path, FileSystemWatcherCallback onChangeCallback) {
    if (!System::get()->pathIsDir(path)) {
        return nullptr;
    }
    return std::make_unique<FileSystemWatcherFS>(path, onChangeCallback);
};
}  // namespace base
}  // namespace android
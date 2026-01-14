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
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "aemu/base/synchronization/Event.h"
#include "android/base/file/file.h"
#include "android/base/file_system_watcher.h"
#include "android/base/win32_unicode_string.h"

#define DEBUG 0
#if DEBUG >= 1
#define DD(fmt, ...) \
    printf("ReadDirectoryChangesWin32: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::base {

// A Very basic file system change detector.
class ReadDirectoryChangesWin32 : public FileSystemWatcher {
  public:
    explicit ReadDirectoryChangesWin32(Path path, FileSystemWatcherCallback on_change_callback)
            : FileSystemWatcher(std::move(on_change_callback)), mPath(std::move(path)) {}

    ~ReadDirectoryChangesWin32() override { Stop(); }

    bool Start() override {
        bool expected = false;
        if (!mRunning.compare_exchange_strong(expected, true)) {
            return false;
        }
        std::thread watcher([this] { watchForChanges(); });
        mWatcherThread = std::move(watcher);
        mStarted.wait();
        return mDirHandle != INVALID_HANDLE_VALUE;
    }

    void Stop() override {
        bool expected = true;
        if (mRunning.compare_exchange_strong(expected, false)) {
            CancelIoEx(mDirHandle, NULL);
            mWatcherThread.join();
        }
    }

  private:
    bool watchForChanges() {
        mDirHandle =
                CreateFileW(mPath.wstring().c_str(), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);

        mStarted.signal();
        if (mDirHandle == INVALID_HANDLE_VALUE) {
            return false;
        }

        while (mRunning) {
            DWORD dwBytesReturned = 0;
            BYTE buffer[4096] = {0};
            if (ReadDirectoryChangesW(mDirHandle, buffer, sizeof(buffer), TRUE,
                                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                              FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                              FILE_NOTIFY_CHANGE_SIZE |
                                              FILE_NOTIFY_CHANGE_LAST_WRITE,
                                      &dwBytesReturned, NULL, NULL) == 0) {
                CloseHandle(mDirHandle);
                mDirHandle = INVALID_HANDLE_VALUE;
                return false;
            }
            DWORD offset = 0;
            while (mRunning) {
                FILE_NOTIFY_INFORMATION* info =
                        reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer + offset);
                Path changed = mPath / std::wstring_view(info->FileName,
                                                         info->FileNameLength / sizeof(wchar_t));
                DD("Action: %d - %s (%d)", info->Action, changed.string().c_str(), offset);
                switch (info->Action) {
                case FILE_ACTION_ADDED:
                    change_callback(WatcherChangeType::kCreated, changed);
                    break;
                case FILE_ACTION_MODIFIED:
                    change_callback(WatcherChangeType::kChanged, changed);
                    break;
                case FILE_ACTION_REMOVED:
                    change_callback(WatcherChangeType::kDeleted, changed);
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    change_callback(WatcherChangeType::kCreated, changed);
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    change_callback(WatcherChangeType::kDeleted, changed);
                    break;
                default:
                    break;
                }

                if (info->NextEntryOffset == 0) {
                    break;
                }

                offset += info->NextEntryOffset;
            }
        }

        CloseHandle(mDirHandle);
        mDirHandle = INVALID_HANDLE_VALUE;
        return true;
    }

    Path mPath;
    HANDLE mDirHandle;
    std::atomic_bool mRunning{false};
    std::thread mWatcherThread;
    Event mStarted;
};

std::unique_ptr<FileSystemWatcher> FileSystemWatcher::GetFileSystemWatcher(
        const Path& path, const FileSystemWatcherCallback& on_change_callback) {
    if (!base::file::is_dir(path)) {
        return nullptr;
    }
    return std::make_unique<ReadDirectoryChangesWin32>(path, on_change_callback);
};
}  // namespace android::base

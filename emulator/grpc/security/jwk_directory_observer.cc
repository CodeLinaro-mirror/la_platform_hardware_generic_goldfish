// Copyright (C) 2022 The Android Open Source Project
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
#include "android/emulation/control/jwk_directory_observer.h"

#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

#include "android/base/file/file.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) \
    printf("JwkDirectoryObserver: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::emulation::control {

using json = nlohmann::json;

JwkDirectoryObserver::JwkDirectoryObserver(const Path& jwks_dir, KeysetUpdatedCallback callback,
                                           PathFilterPredicate filter, bool start_immediately)
        : path_filter_(std::move(filter)), jwk_path_(jwks_dir), callback_(std::move(callback)) {
    watcher_ = FileSystemWatcher::GetFileSystemWatcher(
            jwks_dir,
            [this](auto change, const auto& path) { FileChangeHandler(change, std::move(path)); });

    if (start_immediately) {
        if (!Start()) {
            LOG(WARNING) << "Unable to start observing " << jwks_dir
                         << ", jwks will not be updated. " << loaded_keys_.Size()
                         << " were keysets loaded.";
        }
    }
}

JwkDirectoryObserver::~JwkDirectoryObserver() {
    Stop();
}

bool JwkDirectoryObserver::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    // Initial scan..
    ScanJwkPath();
    NotifyKeysetUpdated();
    return watcher_ ? watcher_->Start() : false;
}

void JwkDirectoryObserver::ScanJwkPath() {
    loaded_keys_.Clear();
    LOG(INFO) << "Scanning " << jwk_path_ << "for jwk keys.";
    for (const auto& path : base::file::scan_dir(jwk_path_, true)) {
        const std::string str_path = path.string();
        auto status = loaded_keys_.Add(str_path);
        if (!status.ok()) {
            LOG(WARNING) << "Failed add jwk key: " << str_path << ", due to: " << status
                         << ", access will be "
                            "denied to this provider and the file deleted.";
            base::file::rm(str_path).IgnoreError();
        }
    };
}

void JwkDirectoryObserver::Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        watcher_->Stop();
    }

    loaded_keys_.Clear();
}

void JwkDirectoryObserver::FileChangeHandler(FileSystemWatcher::WatcherChangeType change,
                                             const Path& path) {
    DD("Filechange handler %d for %s", change, path);
    if (!path_filter_(path)) {
        // Ignore files of the given type.
        DD("Ignoring %s", path);
        return;
    }

    switch (change) {
    case FileSystemWatcher::WatcherChangeType::kCreated:
        [[fallthrough]];
    case FileSystemWatcher::WatcherChangeType::kChanged: {
        DD("Changed/Created event for: %s", path);

        // Wait at most 1 second for non-empty files
        auto status = loaded_keys_.AddWithRetryForEmpty(path, 8, std::chrono::milliseconds(125));
        if (!status.ok()) {
            LOG(WARNING) << "Failed to add jwk key: " << path << ", due to: " << status.message()
                         << ", access will be "
                            "denied to this provider and the file deleted.";
            base::file::rm(path).IgnoreError();
            return;
        }
        LOG(INFO) << "Added JSON Web Key Sets from " << path << ", " << loaded_keys_.Size()
                  << " keys loaded";
        break;
    }
    case FileSystemWatcher::WatcherChangeType::kDeleted:
        DD("Deleted %s", path);
        auto status = loaded_keys_.Remove(path);
        if (!status.ok()) {
            // This usually means it is already deleted.
            LOG(ERROR) << "Failed to remove jwk key: " << path << ", due to: " << status.message();
            return;
        }
        LOG(INFO) << "Removed JSON Web Key Sets from " << path << ", " << loaded_keys_.Size()
                  << " keys loaded";
    };

    NotifyKeysetUpdated();
}

namespace {
absl::Status Notify(const JwkDirectoryObserver::KeysetUpdatedCallback& callback,
                    const JwkKeyLoader& loader) {
    if (loader.Empty()) {
        callback(nullptr);
        return absl::OkStatus();
    }

    auto keyset = loader.ActiveKeySet();
    if (keyset.ok()) {
        callback(std::move(*keyset));
        return absl::OkStatus();
    }

    return keyset.status();
}
}  // namespace

void JwkDirectoryObserver::NotifyKeysetUpdated() {
    auto notified = Notify(callback_, loaded_keys_);

    // Notification failed, lets see if we can rescan and update
    // our keyset..
    if (!notified.ok()) {
        LOG(ERROR) << "Failed construct jwk keyset due to: " << notified.message()
                   << ", rescanning.";
        ScanJwkPath();
        Notify(callback_, loaded_keys_).IgnoreError();
    }
}

bool JwkDirectoryObserver::AcceptJwkExtOnly(const Path& path) {
    return absl::EndsWith(path.string(), kJwkExt);
}

}  // namespace android::emulation::control

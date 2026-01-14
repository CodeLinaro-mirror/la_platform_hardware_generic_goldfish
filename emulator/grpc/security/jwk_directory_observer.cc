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

namespace android {
namespace emulation {
namespace control {

using json = nlohmann::json;

JwkDirectoryObserver::JwkDirectoryObserver(Path jwksDir, KeysetUpdatedCallback callback,
                                           PathFilterPredicate filter, bool startImmediately)
        : mPathFilter(filter), mJwkPath(jwksDir), mCallback(callback) {
    mWatcher = FileSystemWatcher::GetFileSystemWatcher(
            jwksDir, [this](auto change, auto path) { fileChangeHandler(change, path); });

    if (startImmediately) {
        if (!start()) {
            LOG(WARNING) << "Unable to start observing " << jwksDir
                         << ", jwks will not be updated. " << mLoadedKeys.size()
                         << " were keysets loaded.";
        }
    }
}

JwkDirectoryObserver::~JwkDirectoryObserver() {
    stop();
}

bool JwkDirectoryObserver::start() {
    bool expected = false;
    if (!mRunning.compare_exchange_strong(expected, true)) {
        return false;
    }
    // Initial scan..
    scanJwkPath();
    notifyKeysetUpdated();
    return mWatcher ? mWatcher->Start() : false;
}

void JwkDirectoryObserver::scanJwkPath() {
    mLoadedKeys.clear();
    LOG(INFO) << "Scanning " << mJwkPath << "for jwk keys.";
    for (auto path : base::file::scan_dir(mJwkPath, true)) {
        std::string strPath = path.string();
        auto status = mLoadedKeys.add(strPath);
        if (!status.ok()) {
            LOG(WARNING) << "Failed add jwk key: " << strPath << ", due to: " << status
                         << ", access will be "
                            "denied to this provider and the file deleted.";
            base::file::rm(strPath).IgnoreError();
        }
    };
}

void JwkDirectoryObserver::stop() {
    bool expected = true;
    if (!mRunning.compare_exchange_strong(expected, false)) {
        mWatcher->Stop();
    }

    mLoadedKeys.clear();
}

void JwkDirectoryObserver::fileChangeHandler(FileSystemWatcher::WatcherChangeType change,
                                             Path path) {
    DD("Filechange handler %d for %s", change, path);
    if (!mPathFilter(path)) {
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
        auto status = mLoadedKeys.addWithRetryForEmpty(path, 8, std::chrono::milliseconds(125));
        if (!status.ok()) {
            LOG(WARNING) << "Failed to add jwk key: " << path << ", due to: " << status.message()
                         << ", access will be "
                            "denied to this provider and the file deleted.";
            base::file::rm(path).IgnoreError();
            return;
        }
        LOG(INFO) << "Added JSON Web Key Sets from " << path << ", " << mLoadedKeys.size()
                  << " keys loaded";
        break;
    }
    case FileSystemWatcher::WatcherChangeType::kDeleted:
        DD("Deleted %s", path);
        auto status = mLoadedKeys.remove(path);
        if (!status.ok()) {
            // This usually means it is already deleted.
            LOG(ERROR) << "Failed to remove jwk key: " << path << ", due to: " << status.message();
            return;
        }
        LOG(INFO) << "Removed JSON Web Key Sets from " << path << ", " << mLoadedKeys.size()
                  << " keys loaded";
    };

    notifyKeysetUpdated();
}

static absl::Status notify(JwkDirectoryObserver::KeysetUpdatedCallback callback,
                           const JwkKeyLoader& loader) {
    if (loader.empty()) {
        callback(nullptr);
        return absl::OkStatus();
    }

    auto keyset = loader.activeKeySet();
    if (keyset.ok()) {
        callback(std::move(*keyset));
        return absl::OkStatus();
    }

    return keyset.status();
}

void JwkDirectoryObserver::notifyKeysetUpdated() {
    auto notified = notify(mCallback, mLoadedKeys);

    // Notification failed, lets see if we can rescan and update
    // our keyset..
    if (!notified.ok()) {
        LOG(ERROR) << "Failed construct jwk keyset due to: " << notified.message()
                   << ", rescanning.";
        scanJwkPath();
        notify(mCallback, mLoadedKeys).IgnoreError();
    }
}

bool JwkDirectoryObserver::acceptJwkExtOnly(Path path) {
    return absl::EndsWith(path.string(), kJwkExt);
}

}  // namespace control
}  // namespace emulation
}  // namespace android

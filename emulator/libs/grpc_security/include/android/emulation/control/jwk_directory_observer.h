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
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "android/base/file_system_watcher.h"
#include "android/emulation/control/jwk_key_loader.h"
#include "nlohmann/json.hpp"
#include "tink/keyset_handle.h"

namespace android::emulation::control {

using Path = std::filesystem::path;
using base::FileSystemWatcher;
using crypto::tink::KeysetHandle;

// A Jwk Directory Observer observes the given directory for JSON Web Key files.
// A JSON Web Key (JWK) is a JavaScript Object Notation (JSON) data
// structure that represents a cryptographic key.
// (see: https://datatracker.ietf.org/doc/html/rfc7517)
//
// The JwkDirectoryObserver will read all the .jwk files in a directory and
// construct a Tink keyset handle that is a combination of all the keys.
//
// - A callback will be invoked after creation, once all .jwk files have been
//   processed.
// - A callback will be invoked when a .jwk file is added, removed or changed.
//
// Some things to note:
//
// - Only public keys will be handled.
// - Only files with the extension .jwk will be taken in consideration.
// - KID (Key IDentifiers) should be unique across all files.
class JwkDirectoryObserver {
  public:
    using json = nlohmann::json;
    using KeysetUpdatedCallback = std::function<void(std::unique_ptr<KeysetHandle>)>;

    using PathFilterPredicate = std::function<bool(Path)>;

    // Observe the given directory for jwk files, calling
    // the given callback when keyset changes are detected.
    // Set startImmediately to true if start should be called.
    //
    // \jwksDir| Path to observe for file changes
    // \callback| callback to invoke whenever we detect changes
    // |filter| Filter to use, when this predicate return true the file will be
    // processed. |startImmediately| True if we immediately should attempt to
    // observe file changes.
    //
    // Note: setting startImmediately to true means you will not detect failures
    // in the watcher.
    JwkDirectoryObserver(const Path& jwks_dir, KeysetUpdatedCallback callback,
                         PathFilterPredicate filter = JwkDirectoryObserver::AcceptJwkExtOnly,
                         bool start_immediately = true);
    ~JwkDirectoryObserver();

    // Start observing the directory structure.
    // This will invoke the callback if any files are present in the
    // directory that is being observed.
    //
    // Returns false if we are unable to observe the directory.
    bool Start();

    // Stops observing the directory. No new events will be delivered.
    void Stop();

    // The path that is being observed.
    Path Observes() { return jwk_path_; }

  private:
    static bool AcceptJwkExtOnly(const Path& path);
    void FileChangeHandler(FileSystemWatcher::WatcherChangeType change, const Path& path);

    void NotifyKeysetUpdated();
    void ScanJwkPath();

    PathFilterPredicate path_filter_;
    Path jwk_path_;
    JwkKeyLoader loaded_keys_;
    KeysetUpdatedCallback callback_;
    std::unique_ptr<FileSystemWatcher> watcher_;
    std::atomic_bool running_{false};

    static constexpr const std::string_view kJwkExt{".jwk"};
};

}  // namespace android::emulation::control

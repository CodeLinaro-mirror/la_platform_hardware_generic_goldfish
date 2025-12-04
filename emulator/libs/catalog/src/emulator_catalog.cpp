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
#include "goldfish/catalog/emulator_catalog.h"

#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "android/base/system/File.h"
#include "android/files/IniFile.h"
#include "android/goldfish/config/config_dirs.h"

namespace goldfish {

using android::base::FileSystemWatcher;
using android::goldfish::ConfigDirs;
using android::goldfish::IniFile;

std::unique_ptr<EmulatorCatalog> EmulatorCatalog::create(std::filesystem::path discoveryPath) {
    auto path =
            discoveryPath.empty() ? ConfigDirs::getDiscoveryDirectory() : std::move(discoveryPath);

    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        LOG(WARNING) << "Discovery path does not exist or is not a directory: " << path;
        return nullptr;
    }

    // Use a private constructor.
    auto catalog = std::make_unique<EmulatorCatalog>(std::move(path), Private());
    if (!catalog->start()) {
        LOG(WARNING) << "Failed to start the emulator catalog.";
        return nullptr;
    }

    return catalog;
}

EmulatorCatalog::EmulatorCatalog(std::filesystem::path discoveryPath, Private)
        : mDiscoveryPath(std::move(discoveryPath)) {
    mWatcher = FileSystemWatcher::getFileSystemWatcher(
            mDiscoveryPath, [this](auto change, auto path) { onFileChanged(change, path); });
}

EmulatorCatalog::~EmulatorCatalog() {
    stop();
}

bool EmulatorCatalog::start() {
    if (!mWatcher) {
        LOG(ERROR) << "Failed to start FileSystemWatcher, path not a directory: " << mDiscoveryPath;
        return false;
    }
    scanDirectory();
    VLOG(1) << "Watching: " << mDiscoveryPath;
    return mWatcher->start();
}

void EmulatorCatalog::stop() {
    if (mWatcher) {
        mWatcher->stop();
    }
}

std::vector<CatalogEntry> EmulatorCatalog::listEmulators() const {
    absl::MutexLock lock(&mMutex);
    std::vector<CatalogEntry> entries;
    entries.reserve(mEmulators.size());
    for (const auto& [path, entry] : mEmulators) {
        entries.push_back(entry);
    }
    return entries;
}

void EmulatorCatalog::onFileChanged(FileSystemWatcher::WatcherChangeType change,
                                    const std::string& path) {
    // We only care about .ini files.
    VLOG(1) << "EmulatorCatalog: " << path << " changed: " << (int)change;

    if (!absl::EndsWith(path, ".ini")) {
        return;
    }

    VLOG(2) << "EmulatorCatalog: " << path << " changed: " << (int)change;
    switch (change) {
    case FileSystemWatcher::WatcherChangeType::Created:
        addEmulator(path);
        break;
    case FileSystemWatcher::WatcherChangeType::Deleted:
        removeEmulator(path);
        break;
    case FileSystemWatcher::WatcherChangeType::Changed:
        // TODO(jansene): Treat a change as a removal followed by an
        // addition to refresh? removeEmulator(path); addEmulator(path);
        break;
    }
}

void EmulatorCatalog::addEmulator(const std::filesystem::path& path) {
    const auto canonicalPath = std::filesystem::canonical(path);
    IniFile ini(canonicalPath.string());
    if (!ini.read()) {
        LOG(WARNING) << "Could not parse ini file: " << path;
        return;
    }
    EmulatorProperties props;
    for (const auto& key : ini) {
        props[key] = ini.get<std::string>(key, "<unknown>");
    }

    CatalogEntry entry{canonicalPath, props};

    absl::MutexLock lock(&mMutex);
    auto [it, inserted] = mEmulators.insert({canonicalPath, entry});
    if (inserted) {
        VLOG(1) << "Added emulator from: " << canonicalPath;
        emulatorAdded.fireEvent(it->second);
    } else {
        // Update existing entry.
        it->second = entry;
        VLOG(1) << "Updated emulator from: " << canonicalPath;
        emulatorAdded.fireEvent(it->second);
    }
}

void EmulatorCatalog::removeEmulator(const std::filesystem::path& path) {
    absl::MutexLock lock(&mMutex);
    VLOG(1) << "Remove event for: " << path;
    auto it = mEmulators.find(path);
    if (it != mEmulators.end()) {
        CatalogEntry entry = it->second;
        mEmulators.erase(it);
        VLOG(1) << "Removed emulator from: " << path;
        emulatorRemoved.fireEvent(entry);
    }
}

void EmulatorCatalog::scanDirectory() {
    auto files = android::base::file::scan_dir(mDiscoveryPath, true);
    for (const auto& file : files) {
        VLOG(1) << "Discovered: " << file;
        if (absl::EndsWith(file.string(), ".ini")) {
            addEmulator(file);
        }
    }
}

}  // namespace goldfish

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
#pragma once
#include <map>

#include "aemu/base/events/EventSources.h"
#include "aemu/base/files/FileSystemWatcher.h"
#include "android/goldfish/config/emulator_advertisment.h"

namespace goldfish {

using android::base::FileSystemWatcher;
using android::base::eventing::CallbackEventSource;
using android::goldfish::EmulatorProperties;

using ::android::base::FileSystemWatcher;
using ::android::base::eventing::CallbackEventSource;

/**
 * @struct CatalogEntry
 * @brief Represents an emulator advertisement that has been parsed from a file.
 *
 * This struct holds the properties of a discovered emulator instance, along with
 * the path to the INI file from which it was loaded.
 */
struct CatalogEntry {
    /**
     * @brief The canonical path to the emulator's INI file.
     *
     * Using a canonical path ensures that each emulator instance is uniquely
     * identified, even if it's accessed through different paths (e.g., via
     * symbolic links).
     */
    std::filesystem::path path;

    /**
     * @brief A map of key-value pairs representing the emulator's properties.
     */
    EmulatorProperties properties;

    /**
     * @brief Compares two CatalogEntry objects for equality.
     * @param other The other CatalogEntry to compare against.
     * @return True if both the path and properties are identical, false
     * otherwise.
     */
    bool operator==(const CatalogEntry& other) const {
        return path == other.path && properties == other.properties;
    }
};

/**
 * @class EmulatorCatalog
 * @brief Monitors a directory for emulator advertisement files and maintains a
 *        catalog of available emulators.
 *
 * This class scans a specified discovery directory for INI files that
 * advertise running emulator instances. It uses a file system watcher to
 * dynamically update the catalog as emulators are started or stopped.
 *
 * The catalog normalizes all file paths to their canonical form to prevent
 * duplicate entries from symbolic links or relative path differences.
 */
class EmulatorCatalog {
    struct Private {};
  public:
    /**
     * @brief Factory function to create and initialize an EmulatorCatalog
     * instance.
     *
     * This is the designated way to create an EmulatorCatalog. It ensures that
     * the instance is properly initialized and that the file system watcher is
     * started correctly.
     *
     * @param discoveryPath The path to the directory to monitor. If empty, it
     *                      defaults to the standard emulator discovery
     * directory.
     * @return A std::unique_ptr to the created EmulatorCatalog, or nullptr if
     *         initialization fails (e.g., the path is invalid or the watcher
     *         cannot be started).
     */
    static std::unique_ptr<EmulatorCatalog> create(std::filesystem::path discoveryPath = {});

    EmulatorCatalog(std::filesystem::path discoveryPath, Private);
    ~EmulatorCatalog();

    // This class is non-copyable and non-movable to ensure a single owner
    // for the file system watcher.
    EmulatorCatalog(const EmulatorCatalog&) = delete;
    EmulatorCatalog& operator=(const EmulatorCatalog&) = delete;
    EmulatorCatalog(EmulatorCatalog&&) = delete;
    EmulatorCatalog& operator=(EmulatorCatalog&&) = delete;

    /**
     * @brief Returns a list of all currently discovered emulators.
     *
     * This method is thread-safe.
     *
     * @return A vector of CatalogEntry objects, each representing a discovered
     *         emulator.
     */
    std::vector<CatalogEntry> listEmulators() const;

    /**
     * @brief Event fired when a new emulator is discovered.
     *
     * The CatalogEntry provided by the event contains the canonical path to the
     * INI file.
     */
    CallbackEventSource<CatalogEntry> emulatorAdded;

    /**
     * @brief Event fired when an emulator advertisement is removed.
     *
     * The CatalogEntry provided by the event contains the canonical path to the
     * INI file that was removed.
     */
    CallbackEventSource<CatalogEntry> emulatorRemoved;

  private:
    bool start();
    void stop();

    void onFileChanged(FileSystemWatcher::WatcherChangeType change, const std::string& path);
    void addEmulator(const std::filesystem::path& path);
    void removeEmulator(const std::filesystem::path& path);
    void scanDirectory();

    std::filesystem::path mDiscoveryPath;
    std::unique_ptr<android::base::FileSystemWatcher> mWatcher;

    mutable absl::Mutex mMutex;
    std::map<std::filesystem::path, CatalogEntry> mEmulators;
};

}  // namespace goldfish

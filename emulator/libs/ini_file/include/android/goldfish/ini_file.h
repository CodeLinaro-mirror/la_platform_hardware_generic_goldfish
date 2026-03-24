// Copyright 2015 The Android Open Source Project
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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/linked_hash_map.h"

#include "goldfish/file/file.h"

namespace android::goldfish {

class IniFile {
  public:
    IniFile(const IniFile&) = delete;
    const IniFile& operator=(const IniFile&) = delete;

    using DiskSize = uint64_t;

    // linked_hash_map tracks insertion order so that iteration order matches.
    // This allows us to preserve the order of entries when we update the file
    // on disk.
    using MapType = absl::linked_hash_map<std::string, std::string>;

    // Note that the constructor _does not_ read data from the backing file.
    // Call |Read| to read the data.
    // When created without a backing file, all |read|/|write*| operations will
    // fail unless |setBackingFile| is called to point to a valid file path.
    explicit IniFile(fs::path backing_file_path = {})
            : backing_file_path_(std::move(backing_file_path)) {}

    // This constructor reads the data from memory at |data| of |size| bytes.
    IniFile(const char* data, int size);

    // Set a new backing file. This does not read data from the file. Call
    // |read| to refresh data from the new backing file.
    void SetBackingFile(fs::path file_path);
    const fs::path& GetBackingFile() const { return backing_file_path_; }

    // Reads data into IniFile from the backing file, overwriting any
    // existing data.
    bool Read(bool keep_comments = true);
    // Same thing, but parses an already read file data.
    // Note: write operations fail unless there's a backing file set
    bool ReadFromMemory(std::string_view data);

    // Write the current IniFile to the backing file.
    bool Write();
    // Write the current IniFile to backing file. Discard any keys that have
    // empty values.
    bool WriteDiscardingEmpty();
    // An optimized write.
    // - Advantage: We don't write if there have been no updates since last
    // write.
    // - Disadvantage: Not safe if something else might be changing the ini
    //   file -- your view of the file is no longer consistent. Actually, this
    //   "bug" can be considered a "feature", if the ini file changed unbeknown
    //   to you, you're probably doing wrong in overwriting the changes without
    //   any update on your side.
    bool WriteIfChanged();

    // Gets the number of (key,value) pairs in the file.
    int Size() const;
    // Check if a certain key exists in the file.
    bool HasKey(std::string_view key) const;

    // Make sure the string can be used as a valid key/value
    static std::string MakeValidKey(std::string_view str);
    static std::string MakeValidValue(std::string_view str);

    // ///////////////////// Value Getters
    // //////////////////////////////////////
    // The IniFile has no knowledge about the type of the values.
    // |defaultValue| is returned if the key doesn't exist or the value is badly
    // formatted for the requested type.
    //
    // For some value types where the disk format is significantly more useful
    // for human-parsing, overloads are provided that accept default values as
    // strings to be parsed just like the backing ini file.
    // - This has the benefit that default values can be stored in a separate
    //   file in human friendly form, and used directly.
    // - The disadvantage is that behaviour is undefined if we fail to parse the
    //   default value.
    std::string GetString(std::string_view key) const;
    std::string GetString(std::string_view key, std::string_view default_value) const;
    int GetInt(std::string_view key, int default_value) const;
    int64_t GetInt64(std::string_view key, int64_t default_value) const;
    double GetDouble(std::string_view key, double default_value) const;
    // The serialized format for a bool acceepts the following values:
    //     True: "1", "yes", "YES".
    //     False: "0", "no", "NO".
    bool GetBool(std::string_view key, bool default_value) const;
    bool GetBool(std::string_view key, std::string_view default_value_str) const;
    bool GetBool(std::string_view key, const char* default_value) const {
        return GetBool(key, std::string_view(default_value));
    }
    // Parses a string as disk size. The serialized format is [0-9]+[kKmMgG].
    // The
    // suffixes correspond to KiB, MiB and GiB multipliers.
    // Note: We consider 1K = 1024, not 1000.
    DiskSize GetDiskSize(std::string_view key, DiskSize default_value) const;
    DiskSize GetDiskSize(std::string_view key, std::string_view default_value) const;

    // ///////////////////// Value Setters
    // //////////////////////////////////////
    void SetString(std::string key, std::string value);
    void SetInt(std::string key, int value);
    void SetInt64(std::string key, int64_t value);
    void SetDouble(std::string key, double value);
    void SetBool(std::string key, bool value);
    void SetDiskSize(std::string key, DiskSize value);

    // //////////////////// Iterators
    // ///////////////////////////////////////////
    // You can iterate through (string) keys in this IniFile, and then use the
    // correct |get*| function to obtain the corresponding value.
    // The order of keys is guaranteed to be an extension of the order in the
    // backing file:
    //    - For keys that exist in the backing file, order is maintained.
    //    - Rest of the keys are appended in the end, in the order they were
    //      first added.
    //  Only const_iterator is provided. Use |set*| functions to modify the
    //  IniFile.
    MapType::const_iterator begin() const { return std::begin(data_); }
    MapType::const_iterator end() const { return std::end(data_); }

    template <class T>
    T Get(std::string_view key, const T& def) {
        if constexpr (std::is_same_v<T, std::string>) {
            return GetString(key, def);
        } else if constexpr (std::is_same_v<T, int>) {
            return GetInt(key, def);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return GetInt64(key, def);
        } else if constexpr (std::is_same_v<T, double>) {
            return GetDouble(key, def);
        } else if constexpr (std::is_same_v<T, bool>) {
            return GetBool(key, def);
        } else if constexpr (std::is_same_v<T, DiskSize>) {
            return GetDiskSize(key, def);
        } else {
            static_assert(
                    "Unsupported type for Avd::get. Supported types are: "
                    "std::string, int, int64_t, double, bool, DiskSize.");
            return def;
        }
    }

  private:
    void ParseStream(std::istream* in_file, bool keep_comments);
    void UpdateData(std::string key, std::string value);
    bool WriteCommon(bool discard_empty);
    bool WriteCommonImpl(bool discard_empty, const fs::path& file_path);

    MapType data_;
    std::vector<std::pair<int, std::string>> comments_;
    fs::path backing_file_path_;
    bool dirty_ = true;
};

}  // namespace android::goldfish

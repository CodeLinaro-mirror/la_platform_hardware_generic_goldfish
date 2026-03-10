// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/goldfish/ini_file.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <istream>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"

#include "goldfish/file/file.h"
#include "android/base/system.h"

namespace android::goldfish {

namespace fs = std::filesystem;

using std::ifstream;
using std::ios_base;
using std::string;
using std::to_string;

IniFile::IniFile(const char* data, int size) {
    ReadFromMemory(std::string_view(data, size));
}

void IniFile::SetBackingFile(fs::path file_path) {
    // We have no idea what the new backing file contains.
    dirty_ = true;
    backing_file_path_ = std::move(file_path);
}

namespace {

bool IsSpaceChar(unsigned uc) {
    return uc == ' ' || uc == '\r' || uc == '\t';
}

bool IsValueChar(unsigned uc) {
    return uc != '\r' && uc != '\n';
}

bool IsKeyStartChar(unsigned uc) {
    static const unsigned kSmallRange = 'z' - 'a' + 1;
    static const unsigned kCapitalRange = 'Z' - 'A' + 1;
    return (uc - 'a' < kSmallRange) || (uc - 'A' < kCapitalRange) || (uc == '_');
}

bool IsKeyChar(unsigned uc) {
    static const unsigned kNumRange = '9' - '0' + 1;
    return IsKeyStartChar(uc) || (uc - '0' < kNumRange) || (uc == '.') || (uc == '-');
}

template <typename CIterator, typename Pred>
CIterator Eat(CIterator citer, CIterator cend, Pred pred) {
    while (citer != cend && pred(*citer)) {
        ++citer;
    }
    return citer;
}

}  // namespace

void IniFile::ParseStream(std::istream* in, bool keep_comments) {
    string line;
    int lineno = 0;
    // This is the line number we'd print at if the IniFile were immediately
    // written back. Unlike |line|, this will not be incremented for invalid
    // lines, since they're completely dropped.
    int output_lineno = 0;
    while (std::getline(*in, line)) {
        ++lineno;
        ++output_lineno;

        const string& cline = line;
        auto citer = std::begin(cline);
        auto cend = std::end(cline);
        citer = Eat(citer, cend, IsSpaceChar);

        // Handle empty lines, comments.
        if (citer == cend) {
            VLOG(2) << "Line " << lineno << ": Skipped empty line.";
            if (keep_comments) {
                comments_.emplace_back(output_lineno, std::move(line));
            }
            continue;
        }
        if (*citer == '#' || *citer == ';') {
            VLOG(2) << "Line " << lineno << ": Skipped comment line.";
            if (keep_comments) {
                comments_.emplace_back(output_lineno, std::move(line));
            }
            continue;
        }

        // Extract and validate key.
        const auto key_start_iter = citer;
        if (!IsKeyStartChar(*citer)) {
            VLOG(1) << "Line " << lineno << ": Key does not start with a valid character."
                    << " Skipped line.";
            --output_lineno;
            continue;
        }
        ++citer;
        citer = Eat(citer, cend, IsKeyChar);
        auto key = string(key_start_iter, citer);

        // Gobble the = sign.
        citer = Eat(citer, cend, IsSpaceChar);
        if (citer == cend || *citer != '=') {
            VLOG(1) << "Line " << lineno << ": Missing expected assignment operator (=)."
                    << " Skipped line.";
            --output_lineno;
            continue;
        }
        ++citer;

        // Extract the value.
        citer = Eat(citer, cend, IsSpaceChar);
        const auto value_start_iter = citer;
        citer = Eat(citer, cend, IsValueChar);
        auto value = string(value_start_iter, citer);
        // Remove trailing space.
        auto trailing_space_iter = Eat(value.rbegin(), value.rend(), IsSpaceChar);
        value.erase(trailing_space_iter.base(), value.end());

        // Ensure there's no invalid remainder.
        citer = Eat(citer, cend, IsSpaceChar);
        if (citer != cend) {
            VLOG(1) << "Line " << lineno << ": Contains invalid character in the value."
                    << " Skipped line.";
            --output_lineno;
            continue;
        }

        // Everything parsed.
        auto insert_res = data_.emplace(std::move(key), std::string());
        insert_res.first->second = std::move(value);
        if (insert_res.second) {
            order_list_.push_back(&*insert_res.first);
        }
    }
}

bool IniFile::Read(bool keep_comments) {
    dirty_ = false;
    data_.clear();
    order_list_.clear();
    comments_.clear();

    if (backing_file_path_.empty()) {
        LOG(WARNING) << "Read called without a backing file!";
        return false;
    }

    ifstream in_file(backing_file_path_, ios_base::in | ios_base::ate);
    if (!in_file) {
        VLOG(1) << "Failed to process .ini file " << backing_file_path_ << " for reading.";
        return false;
    }

    // avoid reading a very large file that was passed by mistake
    // this threshold is quite liberal.
    static const auto kMaxIniFileSize = ifstream::pos_type(655360);
    static const auto kInvalidPos = ifstream::pos_type(-1);
    const ifstream::pos_type end_pos = in_file.tellg();
    in_file.seekg(0, ios_base::beg);
    const ifstream::pos_type beg_pos = in_file.tellg();
    if (beg_pos == kInvalidPos || end_pos == kInvalidPos || end_pos - beg_pos > kMaxIniFileSize) {
        LOG(WARNING) << ".ini File " << backing_file_path_ << " too large (" << (end_pos - beg_pos)
                     << " bytes)";
        return false;
    }

    ParseStream(&in_file, keep_comments);
    return true;
}

bool IniFile::ReadFromMemory(std::string_view data) {
    dirty_ = false;
    data_.clear();
    order_list_.clear();
    comments_.clear();

    // Create a streambuf that's able to do a single pass over an array only.
    class OnePassIBuf : public std::streambuf {
      public:
        explicit OnePassIBuf(std::string_view data) {
            setg(const_cast<char*>(data.data()), const_cast<char*>(data.data()),
                 const_cast<char*>(data.data()) + data.size());
        }
    };
    OnePassIBuf ibuf(data);
    std::istream in(&ibuf);
    if (!in) {
        LOG(WARNING) << "Failed to process input data for reading.";
        return false;
    }

    ParseStream(&in, true);
    return true;
}

bool IniFile::WriteCommonImpl(bool discard_empty, const fs::path& file_path) {
    std::ofstream out_file(file_path, std::ios_base::out | std::ios_base::trunc);

    if (!out_file) {
        LOG(WARNING) << "Failed to open '" << file_path << "' for writing.";
        return false;
    }

    int lineno = 0;
    auto comment_iter = std::begin(comments_);
    for (const auto& pair : order_list_) {
        ++lineno;

        // Write comments
        for (; comment_iter != std::end(comments_) && lineno >= comment_iter->first;
             ++comment_iter, ++lineno) {
            out_file << comment_iter->second << "\n";
        }

        const string& value = pair->second;
        if (discard_empty && value.empty()) {
            continue;
        }
        const string& key = pair->first;
        out_file << key << " = " << value << '\n';
    }

    dirty_ = false;
    return true;
}

// 1. write config to mBackingFilePath.new
// 2. rename mBackingFilePath to mBackingFilePath.old
// 3. rename mBackingFilePath.new to mBackingFilePath
// 4. delete mBackingFilePath.old
bool IniFile::WriteCommon(const bool discard_empty) {
    if (backing_file_path_.empty()) {
        LOG(WARNING) << "Write called without a backing file!";
        return false;
    }

    fs::path ini_file_new = backing_file_path_;
    ini_file_new += ".new";
    if (!WriteCommonImpl(discard_empty, ini_file_new)) {
        return false;
    }

    fs::path ini_file_old = backing_file_path_;
    ini_file_old += ".old";
    base::file::rm(ini_file_old).IgnoreError();  // just in case `myRemove` below failed

    const bool delete_old_config = base::file::mv_file(backing_file_path_, ini_file_old).ok();

    if (!base::file::mv_file(ini_file_new, backing_file_path_).ok()) {
        if (delete_old_config) {
            // try to revert the first `rename`
            if (!base::file::mv_file(ini_file_old, backing_file_path_).ok()) {
                // mBackingFilePath is missing here
                LOG(ERROR) << "Failed to update '" << backing_file_path_.string()
                           << "', the file no longer exists";
            } else {
                // mBackingFilePath is reverted back
                LOG(WARNING) << "Failed to update '" << backing_file_path_.string() << "'";
            }
        } else {
            // mBackingFilePath could be read-only
            LOG(WARNING) << "Failed to save '" << backing_file_path_.string() << "'";
        }

        base::file::rm(ini_file_new).IgnoreError();
        return false;
    }

    if (delete_old_config) {
        base::file::rm(ini_file_old).IgnoreError();
    }

    return true;
}

bool IniFile::Write() {
    return WriteCommon(false);
}

bool IniFile::WriteDiscardingEmpty() {
    return WriteCommon(true);
}

bool IniFile::WriteIfChanged() {
    return !dirty_ || WriteCommon(false);
}

int IniFile::Size() const {
    return static_cast<int>(data_.size());
}

bool IniFile::HasKey(std::string_view key) const {
    return data_.find(std::string(key)) != std::end(data_);
}

std::string IniFile::MakeValidKey(std::string_view str) {
    std::ostringstream res;
    res << std::hex << std::uppercase;
    res << '_';  // mark all keys passed through this function with a leading
                 // underscore
    for (const char c : str) {
        if (IsKeyChar(c)) {
            res << c;
        } else {
            res << '.' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return res.str();
}

string IniFile::MakeValidValue(std::string_view str) {
    std::ostringstream res;
    for (const auto& ch : str) {
        if (ch == '%') res << ch;
        res << ch;
    }
    return res.str();
}
namespace {
// Substitute environment variables inside a string.
// Substitution is based on %ENVIRONMENT_VARIABLE%
// Double %% is treated as an escape.
// If a string is not terminated (i.e a starting %, but
// no ending %) the whole string will be returned.
//
// For example:
// "%PAGER%" => "less"
// "%PAG%%ER%" => "" ("PAG%ER" is not a valid name)
// "%FOO" => "%FOO" (Not terminated)
// "%%HI%%" => "%HI%" (Escaped)
// Note that: %%%USER%%% is parsed as %(USER)% and not %(USER%)%
string EnvSubst(const std::string_view fix) {
    const size_t len = fix.size();

    string res;
    string var;
    string* curr = &res;
    for (unsigned int i = 0; i < len; i++) {
        const char ch = fix[i];

        // A normal character will be added to the current string
        if (ch != '%') {
            curr->push_back(ch);
            continue;
        }

        // Let's see if we are closing
        if (curr == &var) {
            const string env = base::System::Get()->EnvGet(var);
            if (env.empty()) {
                LOG(WARNING) << "Environment variable " << var << " is not set";
            }
            res.append(env);
            var.clear();
            curr = &res;
            continue;
        }

        // Check if we have a case of escapism..
        // Note that len-1 >= 0
        const char next = (i < len - 1) ? fix[i + 1] : '\0';

        if (next == '%') {
            // Escaped, let's skip it.
            curr->push_back(ch);
            i++;
        } else {
            // We open and start a env variable name.
            curr = &var;
        }
    }

    // Return full string for unterminated piece.
    if (curr == &var) {
        res.push_back('%');
        res.append(var);
    }

    return res;
}

#if defined(_WIN32)
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

bool IsBoolTrue(std::string_view value) {
    const char* cstr = value.data();
    const size_t size = value.size();

    return strncasecmp("yes", cstr, size) == 0 || strncasecmp("true", cstr, size) == 0 ||
           strncasecmp("1", cstr, size) == 0;
}

bool IsBoolFalse(std::string_view value) {
    const char* cstr = value.data();
    const size_t size = value.size();
    return strncasecmp("no", cstr, size) == 0 || strncasecmp("false", cstr, size) == 0 ||
           strncasecmp("0", cstr, size) == 0;
}

// If not nullptr, |*out_malformed| is set to true if |value_str| is malformed.
IniFile::DiskSize ParseDiskSize(std::string_view value_str, IniFile::DiskSize default_value,
                                bool* out_malformed) {
    if (out_malformed) {
        *out_malformed = false;
    }

    char* end;
    errno = 0;
    const std::string safe_str = std::string(value_str);
    IniFile::DiskSize result = strtoll(safe_str.c_str(), &end, 10);
    bool malformed = (errno != 0);
    if (!malformed) {
        switch (*end) {
        case 0:
            break;
        case 'k':
        case 'K':
            result *= 1024ULL;
            break;
        case 'm':
        case 'M':
            result *= 1024 * 1024ULL;
            break;
        case 'g':
        case 'G':
            result *= 1024ULL * 1024 * 1024;
            break;
        default:
            malformed = true;
        }
    }

    if (malformed) {
        if (out_malformed) {
            *out_malformed = true;
        }
        return default_value;
    }
    return result;
}

}  // namespace

string IniFile::GetString(const string& key, std::string_view default_value) const {
    auto citer = data_.find(key);
    return EnvSubst((citer == std::end(data_)) ? default_value : std::string_view(citer->second));
}

int IniFile::GetInt(const string& key, int default_value) const {
    if (data_.find(key) == std::end(data_)) {
        return default_value;
    }

    auto value = GetString(key, "");
    char* end;
    errno = 0;
    const int result = static_cast<int>(strtol(value.c_str(), &end, 10));
    if (errno || *end != 0) {
        VLOG(1) << "Malformed int value " << value << " for key " << key;
        return default_value;
    }
    return result;
}

int64_t IniFile::GetInt64(const string& key, int64_t default_value) const {
    if (data_.find(key) == std::end(data_)) {
        return default_value;
    }

    auto value = GetString(key, "");
    char* end;
    errno = 0;
    const int64_t result = strtoll(value.c_str(), &end, 10);
    if (errno || *end != 0) {
        VLOG(1) << "Malformed int64 value " << value << " for key " << key;
        return default_value;
    }
    return result;
}

double IniFile::GetDouble(const string& key, double default_value) const {
    if (data_.find(key) == std::end(data_)) {
        return default_value;
    }

    auto value = GetString(key, "");
    char* end;
    errno = 0;
    const double result = strtod(value.c_str(), &end);
    if (errno || *end != 0) {
        VLOG(1) << "Malformed double value " << value << " for key " << key;
        return default_value;
    }
    return result;
}

bool IniFile::GetBool(const string& key, bool default_value) const {
    if (data_.find(key) == std::end(data_)) {
        return default_value;
    }

    const string& value = GetString(key, "");
    if (IsBoolTrue(value)) {
        return true;
    }
    if (IsBoolFalse(value)) {
        return false;
    }
    VLOG(1) << "Malformed bool value " << value << " for key " << key;
    return default_value;
}

bool IniFile::GetBool(const string& key, std::string_view default_value) const {
    return GetBool(key, IsBoolTrue(default_value));
}
IniFile::DiskSize IniFile::GetDiskSize(const string& key, IniFile::DiskSize default_value) const {
    if (!HasKey(key)) {
        return default_value;
    }
    bool malformed = false;
    auto value = GetString(key, "");
    const IniFile::DiskSize result = ParseDiskSize(value, default_value, &malformed);

    LOG_IF(INFO, malformed) << "Malformed DiskSize value " << value << " for key " << key;
    return result;
}

IniFile::DiskSize IniFile::GetDiskSize(const string& key, std::string_view default_value) const {
    return GetDiskSize(key, ParseDiskSize(default_value, 0, nullptr));
}

void IniFile::UpdateData(const string& key, string&& value) {
    dirty_ = true;
    // note: may not move here, as it's currently unspecified if failed
    //  insertion moves from |value| or not. Move it separately instead.
    auto result = data_.emplace(key, std::string());
    result.first->second = std::move(value);
    if (result.second) {
        // New element was created.
        order_list_.push_back(&*result.first);
    }
}

void IniFile::SetString(const string& key, std::string_view value) {
    UpdateData(key, std::string(value));
}

void IniFile::SetInt(const string& key, int value) {
    UpdateData(key, to_string(value));
}

void IniFile::SetInt64(const string& key, int64_t value) {
    // long long is at least 64 bit in C++0x.
    UpdateData(key, to_string(static_cast<long long>(value)));
}

void IniFile::SetDouble(const string& key, double value) {
    UpdateData(key, to_string(value));
}

void IniFile::SetBool(const string& key, bool value) {
    UpdateData(key, value ? "true" : "false");
}

void IniFile::SetDiskSize(const string& key, DiskSize value) {
    static const DiskSize kKilo = 1024;
    static const DiskSize kMega = 1024 * kKilo;
    static const DiskSize kGiga = 1024 * kMega;

    char suffix = 0;
    if (value >= kGiga && !(value % kGiga)) {
        value /= kGiga;
        suffix = 'g';
    } else if (value >= kMega && !(value % kMega)) {
        value /= kMega;
        suffix = 'm';
    } else if (value >= kKilo && !(value % kKilo)) {
        value /= kKilo;
        suffix = 'k';
    }

    auto value_str = to_string(value);
    if (suffix) {
        value_str += suffix;
    }
    UpdateData(key, std::move(value_str));
}

}  // namespace android::goldfish

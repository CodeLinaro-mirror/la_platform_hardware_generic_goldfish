/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#include "android/utils/dirscanner.h"

#include "android/base/system/System.h"
#include "aemu/base/files/PathUtils.h"

#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include "aemu/base/system/Win32UnicodeString.h"
using android::base::Win32UnicodeString;
#endif

using android::base::PathUtils;
namespace fs = std::filesystem;

struct DirScanner {
    std::vector<std::filesystem::path> entries;
    std::vector<std::string> strEntries;
    fs::path prefix;
    std::string result_str;
    size_t pos;

    explicit DirScanner(fs::path dir) :
            entries(),
            prefix(dir),
            result_str(),
            pos(0u) {
        entries = android::base::System::get()->scanDirEntries(dir);
        for(const auto& entry : entries) {

#ifdef _WIN32
            auto converted = Win32UnicodeString(entry.string());
            strEntries.push_back(converted.toString());
#else
            strEntries.push_back(entry.string());
#endif
        }
    }

};

const char* dirScanner_next(DirScanner* s) {
    if (s->pos < s->entries.size()) {
        return s->strEntries[s->pos++].c_str();
    }
    return NULL;
}

const char* dirScanner_nextFull(DirScanner* s) {
    if (s->pos < s->strEntries.size()) {
        auto path =  s->prefix / s->entries[s->pos++];
#if _WIN32
        s->result_str = Win32UnicodeString(path.string()).toString();
#else
        s->result_str = path.string();
#endif
        return s->result_str.c_str();
    }
    return NULL;
}

size_t dirScanner_numEntries(DirScanner* s) {
    return s->entries.size();
}

DirScanner* dirScanner_new(const char* rootPath) {
    #ifdef _WIN32
    auto root = Win32UnicodeString(rootPath);
    DirScanner* s = new DirScanner(root.toString());
    #else
    DirScanner* s = new DirScanner(rootPath);
    #endif
    return s;
}

void dirScanner_free(DirScanner* s) {
    delete s;
}

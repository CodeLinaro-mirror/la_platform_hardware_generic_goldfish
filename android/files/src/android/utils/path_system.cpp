/* Copyright (C) 2007-2009 The Android Open Source Project
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

#include <android/utils/system.h>

#include <filesystem>
#include <string>

#include "aemu/base/files/PathUtils.h"
#include "aemu/base/memory/ScopedPtr.h"
#include "aemu/base/misc/StringUtils.h"
#include "aemu/base/system/Win32UnicodeString.h"
#include "android/base/system/System.h"
#include "android/utils/path.h"

using android::base::PathUtils;
using android::base::ScopedCPtr;
using android::base::System;
namespace fs = std::filesystem;

ABool path_exists(const char* path) {
    if (!path) return false;
    return System::get()->pathExists(path);
}

ABool path_is_regular(const char* path) {
    if (!path) return false;
    return System::get()->pathIsFile(path);
}

ABool path_is_dir(const char* path) {
    if (!path) return false;
    return System::get()->pathIsDir(path);
}

ABool path_can_read(const char* path) {
    if (!path) return false;
    return System::get()->pathCanRead(path);
}

ABool path_can_write(const char* path) {
    if (!path) return false;
    return System::get()->pathCanWrite(path);
}

ABool path_can_exec(const char* path) {
    if (!path) return false;
    return System::get()->pathCanExec(path);
}

int path_open(const char* filename, int oflag, int pmode) {
    return System::get()->pathOpen(filename, oflag, pmode);
}

ABool path_is_absolute(const char* path) {
    return PathUtils::isAbsolute(path);
}

char* path_get_absolute(const char* path) {
    if (path_is_absolute(path)) {
        return ASTRDUP(path);
    }

    fs::path absolute = fs::absolute(System::get()->getCurrentDirectory() / path);
    return ASTRDUP(absolute.string().c_str());
}

int path_split(const char* path, char** dirname, char** basename) {
    std::string dir, file;
    if (!PathUtils::split(path, &dir, &file)) {
        return -1;
    }
    if (dirname) {
        *dirname = ASTRDUP(dir.c_str());
    }
    if (basename) {
        *basename = ASTRDUP(file.c_str());
    }
    return 0;
}

char* path_dirname(const char* path) {
    std::string dir;
    if (!PathUtils::split(path, &dir, nullptr)) {
        return nullptr;
    }
    return ASTRDUP(dir.c_str());
}

char* path_basename(const char* path) {
    std::string file;
    if (!PathUtils::split(path, nullptr, &file)) {
        return nullptr;
    }
    return ASTRDUP(file.c_str());
}

#ifdef _WIN32
using android::base::Win32UnicodeString;

extern "C" char* realpath_with_length(const char* path, char* resolved_path, size_t max_length) {
    Win32UnicodeString widePath(path);
    // Let the call allocate memory for us, then we can check against max_length
    ScopedCPtr<wchar_t> result(_wfullpath(nullptr, widePath.c_str(), 0));
    if (result.get() == nullptr) {
        return nullptr;
    }
    auto utf8Path = Win32UnicodeString::convertToUtf8(result.get());
    if (resolved_path == nullptr) {
        // Passing in a null pointer is valid and should lead to allocation
        // without checking the length argument
        return ASTRDUP(utf8Path.c_str());
    }

    if (utf8Path.size() + 1 >= max_length) {
        // This mimics the behavior of _wfullpath where if the buffer is not
        // sufficiently large to hold the string plus a null terminator the call
        // will fail and return nullptr
        return nullptr;
    }
    strcpy(resolved_path, utf8Path.c_str());
    return resolved_path;
}
#endif  // _WIN32

// Copyright (C) 2015 The Android Open Source Project
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
#include "android/base/win32_unicode_string.h"

#include <string.h>
#include <windows.h>

#include <algorithm>

namespace android::base {

// NOLINTBEGIN
Win32UnicodeString::Win32UnicodeString() : str_(nullptr), size_(0u) {}

Win32UnicodeString::Win32UnicodeString(const char* str, size_t len) : str_(nullptr), size_(0u) {
    reset(str, strlen(str));
}

Win32UnicodeString::Win32UnicodeString(const char* str) : str_(nullptr), size_(0u) {
    reset(str);
}

Win32UnicodeString::Win32UnicodeString(const std::string& str) : str_(nullptr), size_(0u) {
    reset(str.c_str());
}

Win32UnicodeString::Win32UnicodeString(size_t size) : str_(nullptr), size_(0u) {
    resize(size);
}

Win32UnicodeString::Win32UnicodeString(const wchar_t* str) : str_(nullptr), size_(0u) {
    size_t len = str ? wcslen(str) : 0u;
    resize(len);
    ::memcpy(str_, str ? str : L"", len * sizeof(wchar_t));
}

Win32UnicodeString::Win32UnicodeString(const Win32UnicodeString& other) : str_(nullptr), size_(0u) {
    resize(other.size_);
    ::memcpy(str_, other.c_str(), other.size_ * sizeof(wchar_t));
}

Win32UnicodeString::~Win32UnicodeString() {
    delete[] str_;
}

Win32UnicodeString& Win32UnicodeString::operator=(const Win32UnicodeString& other) {
    resize(other.size_);
    ::memcpy(str_, other.c_str(), other.size_ * sizeof(wchar_t));
    return *this;
}

Win32UnicodeString& Win32UnicodeString::operator=(const wchar_t* str) {
    size_t len = str ? wcslen(str) : 0u;
    resize(len);
    ::memcpy(str_, str ? str : L"", len * sizeof(wchar_t));
    return *this;
}

wchar_t* Win32UnicodeString::data() {
    if (!str_) {
        // Ensure the function never returns NULL.
        // it is safe to const_cast the pointer here - user isn't allowed to
        // write into it anyway
        return const_cast<wchar_t*>(L"");
    }
    return str_;
}

std::string Win32UnicodeString::toString() const {
    return convertToUtf8(str_, size_);
}

void Win32UnicodeString::reset(const char* str, size_t len) {
    if (str_) {
        delete[] str_;
    }
    const int utf16Len = calcUtf16BufferLength(str, len);
    str_ = new wchar_t[utf16Len + 1];
    size_ = static_cast<size_t>(utf16Len);
    convertFromUtf8(str_, utf16Len, str, len);
    str_[size_] = L'\0';
}

void Win32UnicodeString::reset(const char* str) {
    reset(str, strlen(str));
}

void Win32UnicodeString::resize(size_t newSize) {
    if (newSize == 0) {
        delete[] str_;
        str_ = nullptr;
        size_ = 0;
    } else if (newSize <= size_) {
        str_[newSize] = 0;
        size_ = newSize;
    } else {
        wchar_t* oldStr = str_;
        str_ = new wchar_t[newSize + 1u];
        size_t copySize = std::min<size_t>(newSize, size_);
        ::memcpy(str_, oldStr ? oldStr : L"", copySize * sizeof(wchar_t));
        str_[copySize] = L'\0';
        str_[newSize] = L'\0';
        size_ = newSize;
        delete[] oldStr;
    }
}

void Win32UnicodeString::append(const wchar_t* str) {
    append(str, wcslen(str));
}

void Win32UnicodeString::append(const wchar_t* str, size_t len) {
    // NOTE: This method should be rarely used, so don't try to optimize
    // storage with larger capacity values and exponential increments.
    if (!str || !len) {
        return;
    }
    size_t oldSize = size();
    resize(oldSize + len);
    memmove(str_ + oldSize, str, len * sizeof(wchar_t));
}

void Win32UnicodeString::append(const Win32UnicodeString& other) {
    append(other.c_str(), other.size());
}

wchar_t* Win32UnicodeString::release() {
    wchar_t* result = str_;
    str_ = nullptr;
    size_ = 0u;
    return result;
}

// static
std::string Win32UnicodeString::convertToUtf8(const wchar_t* str, int len) {
    std::string result;
    const int utf8Len = calcUtf8BufferLength(str, len);
    if (utf8Len > 0) {
        result.resize(static_cast<size_t>(utf8Len));
        convertToUtf8(&result[0], utf8Len, str, len);
        if (len == -1) {
            result.resize(utf8Len - 1);  // get rid of the null-terminator
        }
    }
    return result;
}

// returns the return value of a Win32UnicodeString public conversion function
// from a WinAPI conversion function returned code
static int convertRetVal(int winapiResult) {
    return winapiResult ? winapiResult : -1;
}

// static
int Win32UnicodeString::calcUtf8BufferLength(const wchar_t* str, int len) {
    if (len < 0 && len != -1) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    const int utf8Len = WideCharToMultiByte(CP_UTF8,   // CodePage
                                            0,         // dwFlags
                                            str,       // lpWideCharStr
                                            len,       // cchWideChar
                                            nullptr,   // lpMultiByteStr
                                            0,         // cbMultiByte
                                            nullptr,   // lpDefaultChar
                                            nullptr);  // lpUsedDefaultChar

    return convertRetVal(utf8Len);
}

// static
int Win32UnicodeString::calcUtf16BufferLength(const char* str, int len) {
    if (len < 0 && len != -1) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    const int utf16Len = MultiByteToWideChar(CP_UTF8,  // CodePage
                                             0,        // dwFlags
                                             str,      // lpMultiByteStr
                                             len,      // cbMultiByte
                                             nullptr,  // lpWideCharStr
                                             0);       // cchWideChar

    return convertRetVal(utf16Len);
}

// static
int Win32UnicodeString::convertToUtf8(char* outStr, int outLen, const wchar_t* str, int len) {
    if (!outStr || outLen < 0 || !str || (len < 0 && len != -1)) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    const int utf8Len = WideCharToMultiByte(CP_UTF8,   // CodePage
                                            0,         // dwFlags
                                            str,       // lpWideCharStr
                                            len,       // cchWideChar
                                            outStr,    // lpMultiByteStr
                                            outLen,    // cbMultiByte
                                            nullptr,   // lpDefaultChar
                                            nullptr);  // lpUsedDefaultChar
    return convertRetVal(utf8Len);
}

// static
int Win32UnicodeString::convertFromUtf8(wchar_t* outStr, int outLen, const char* str, int len) {
    if (!outStr || outLen < 0 || !str || (len < 0 && len != -1)) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    const int utf16Len = MultiByteToWideChar(CP_UTF8,  // CodePage
                                             0,        // dwFlags
                                             str,      // lpMultiByteStr
                                             len,      // cbMultiByte
                                             outStr,   // lpWideCharStr
                                             outLen);  // cchWideChar
    return convertRetVal(utf16Len);
}

// NOLINTEND

}  // namespace android::base

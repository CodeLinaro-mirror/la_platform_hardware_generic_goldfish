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

#include "android/base/system.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include "android/base/bazel_info.h"
#include "android/base/c_str_wrapper.h"
#include "goldfish/file/storage_capacity.h"
#include "android/process/command.h"

#ifdef _WIN32

// IWYU pragma: end_keep
// clang-format on
#include <ntddscsi.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <windows.h>
#include <winioctl.h>

#include "android/base/scoped_reg_key.h"
#include "android/base/win32_unicode_string.h"
#include "android/base/win32_utils.h"
// IWYU pragma: end_keep
// clang-format on

#endif

#ifdef __APPLE__
#import <CoreFoundation/CoreFoundation.h>
#include <libproc.h>
#include <mach/clock.h>
#include <mach/mach.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/types.h>

// Instead of including this private header let's copy its important
// definitions in.
// #include <CoreFoundation/CFPriv.h>
extern "C" {
/* System Version file access */
CF_EXPORT CFDictionaryRef _CFCopySystemVersionDictionary(void);
CF_EXPORT CFDictionaryRef _CFCopyServerVersionDictionary(void);
CF_EXPORT const CFStringRef _kCFSystemVersionProductNameKey;
CF_EXPORT const CFStringRef _kCFSystemVersionProductVersionKey;
}  // extern "C"
#endif  // __APPLE__

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/statvfs.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <ctime>
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#ifdef _MSC_VER
#include "dirent.h"
extern "C" {
#include <sys/time.h>
#include <unistd.h>
}
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#endif

// This variable is a pointer to a zero-terminated array of all environment
// variables in the current process.
// Posix requires this to be declared as extern at the point of use
// NOTE: Apple developer manual states that this variable isn't available for
// the shared libraries, and one has to use the _NSGetEnviron() function instead
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#include <sys/utsname.h>
#endif
#ifdef __linux__
extern "C" char** environ;
#endif

#include "goldfish/file/file.h"

namespace android {
namespace base {
namespace fs = std::filesystem;

// The character used to separator directories in path-related
// environment variables.
#ifdef _WIN32
constexpr char kPathSeparator = ';';
#else
constexpr char kPathSeparator = ':';
#endif
namespace {

struct TickCountImpl {
  private:
    System::WallDuration mStartTimeUs;
#ifdef _WIN32
    long long mFreqPerSec = 0;  // 0 means 'high perf counter isn't available'
#elif defined(__APPLE__)
    clock_serv_t mClockServ;
#endif

  public:
    TickCountImpl() {
#ifdef _WIN32
        LARGE_INTEGER freq;
        if (::QueryPerformanceFrequency(&freq)) {
            mFreqPerSec = freq.QuadPart;
        }
#elif defined(__APPLE__)
        host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &mClockServ);
#endif
        mStartTimeUs = getUs();
    }

#ifdef __APPLE__
    ~TickCountImpl() { mach_port_deallocate(mach_task_self(), mClockServ); }
#endif

    System::WallDuration getStartTimeUs() const { return mStartTimeUs; }

    System::WallDuration getUs() const {
#ifdef _WIN32
        if (!mFreqPerSec) {
            return ::GetTickCount() * 1000;
        }
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        return (now.QuadPart * 1000000ull) / mFreqPerSec;
#elif defined __linux__
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000ll + ts.tv_nsec / 1000;
#else  // APPLE
        mach_timespec_t mts;
        clock_get_time(mClockServ, &mts);
        return mts.tv_sec * 1000000ll + mts.tv_nsec / 1000;
#endif
    }
};

// This is, maybe, the only static variable that may not be a LazyInstance:
// it holds the actual timestamp at startup, and has to be initialized as
// soon as possible after the application launch.
const TickCountImpl kTickCount;

}  // namespace

namespace {

bool parseBooleanValue(const char* value, bool def) {
    if (0 == strcmp(value, "1")) {
        return true;
    }
    if (0 == strcmp(value, "y")) {
        return true;
    }
    if (0 == strcmp(value, "yes")) {
        return true;
    }
    if (0 == strcmp(value, "Y")) {
        return true;
    }
    if (0 == strcmp(value, "YES")) {
        return true;
    }

    if (0 == strcmp(value, "0")) {
        return false;
    }
    if (0 == strcmp(value, "n")) {
        return false;
    }
    if (0 == strcmp(value, "no")) {
        return false;
    }
    if (0 == strcmp(value, "N")) {
        return false;
    }
    if (0 == strcmp(value, "NO")) {
        return false;
    }

    return def;
}

class HostSystem : public System {
  public:
    HostSystem() : mHomeDir(), mAppDataDir() {
        ::atexit(HostSystem::atexit_HostSystem);
        ConfigureHost();
    }

    ~HostSystem() override {}

    const fs::path GetHomeDirectory() const override {
        if (mHomeDir.empty()) {
#if defined(_WIN32)
            // NOTE: SHGetFolderPathW always takes a buffer of MAX_PATH size,
            // so don't use a Win32UnicodeString here to avoid unnecessary
            // dynamic allocation.
            wchar_t path[MAX_PATH] = {0};
            // Query Windows shell for known folder paths.
            // SHGetFolderPath acts as a wrapper to KnownFolders;
            // this is preferred for simplicity and XP compatibility.
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
                mHomeDir = Win32UnicodeString::convertToUtf8(path);
            } else {
                // Fallback to windows-equivalent of HOME env var
                std::string homedrive = EnvGet("HOMEDRIVE");
                std::string homepath = EnvGet("HOMEPATH");
                if (!homedrive.empty() && !homepath.empty()) {
                    mHomeDir.assign(homedrive);
                    mHomeDir.append(homepath);
                }
            }
#elif defined(__linux__) || (__APPLE__)
            // Try getting HOME from env first
            const char* home = getenv("HOME");
            if (home != nullptr) {
                mHomeDir.assign(home);
            } else {
                // If env HOME appears empty for some reason,
                // try getting HOME by querying system password database
                const struct passwd* pw = getpwuid(getuid());
                if (pw != nullptr && pw->pw_dir != nullptr) {
                    mHomeDir.assign(pw->pw_dir);
                }
            }
#else
#error "Unsupported platform!"
#endif
        }
        return mHomeDir;
    }

    const fs::path GetAppDataDirectory() const override {
#if defined(_WIN32)
        if (mAppDataDir.empty()) {
            // NOTE: See comment in GetHomeDirectory().
            wchar_t path[MAX_PATH] = {0};
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
                mAppDataDir = Win32UnicodeString::convertToUtf8(path);
            } else {
                const wchar_t* appdata = _wgetenv(L"APPDATA");
                if (appdata != NULL) {
                    mAppDataDir = Win32UnicodeString::convertToUtf8(appdata);
                }
            }
        }
#elif defined(__APPLE__)
        if (mAppDataDir.empty()) {
            // The equivalent of AppData directory in MacOS X is
            // under ~/Library/Preferences. Apple does not offer
            // a C/C++ API to query this location (in ObjC cocoa
            // applications NSSearchPathForDirectoriesInDomains
            // can be used), so we apply the common practice of
            // hard coding it
            mAppDataDir.assign(GetHomeDirectory());
            mAppDataDir.append("/Library/Preferences");
        }
#elif defined(__linux__)
        ;  // not applicable
#else
#error "Unsupported platform!"
#endif
        return mAppDataDir;
    }

    OsType GetOsType() const override {
#ifdef _WIN32
        return OsType::kWindows;
#elif defined(__APPLE__)
        return OsType::kMac;
#elif defined(__linux__)
        return OsType::kLinux;
#else
#error getOsType(): unsupported OS;
#endif
    }

    std::string GetOsName() override {
        static std::string lastSuccessfulValue;
        if (!lastSuccessfulValue.empty()) {
            return lastSuccessfulValue;
        }
#ifdef _WIN32
        HKEY hkey = 0;
        LONG result =
                RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                              0, KEY_READ, &hkey);
        if (result != ERROR_SUCCESS) {
            std::string errorStr = absl::StrFormat("Error: RegOpenKeyExA failed %ld %s", result,
                                                   Win32Utils::getErrorString(result));
            VLOG(1) << errorStr;
            return errorStr;
        }
        ScopedRegKey hOsVersionKey(hkey);

        DWORD osNameSize = 0;
        const WCHAR productNameKey[] = L"ProductName";
        result = RegGetValueW(hOsVersionKey.get(), nullptr, productNameKey, RRF_RT_REG_SZ, nullptr,
                              nullptr, &osNameSize);
        if (result != ERROR_SUCCESS && ERROR_MORE_DATA != result) {
            std::string errorStr = absl::StrFormat("Error: RegGetValueW failed %ld %s", result,
                                                   Win32Utils::getErrorString(result));
            VLOG(1) << errorStr;
            return errorStr;
        }

        Win32UnicodeString osName;
        osName.resize((osNameSize - 1) / sizeof(wchar_t));
        result = RegGetValueW(hOsVersionKey.get(), nullptr, productNameKey, RRF_RT_REG_SZ, nullptr,
                              osName.data(), &osNameSize);
        if (result != ERROR_SUCCESS) {
            std::string errorStr = absl::StrFormat("Error: RegGetValueW failed %ld %s", result,
                                                   Win32Utils::getErrorString(result));
            VLOG(1) << errorStr;
            return errorStr;
        }
        lastSuccessfulValue = osName.toString();
        return lastSuccessfulValue;
#elif defined(__APPLE__)
        // Taken from
        // https://opensource.apple.com/source/DarwinTools/DarwinTools-1/sw_vers.c
        /*
         * Copyright (c) 2005 Finlay Dobbie
         * All rights reserved.
         *
         * Redistribution and use in source and binary forms, with or without
         * modification, are permitted provided that the following conditions
         * are met:
         * 1. Redistributions of source code must retain the above copyright
         *    notice, this list of conditions and the following disclaimer.
         * 2. Redistributions in binary form must reproduce the above copyright
         *    notice, this list of conditions and the following disclaimer in the
         *    documentation and/or other materials provided with the distribution.
         * 3. Neither the name of Finlay Dobbie nor the names of his contributors
         *    may be used to endorse or promote products derived from this software
         *    without specific prior written permission.
         *
         * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
         * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
         * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
         * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
         * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
         * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
         * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
         * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
         * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
         * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
         * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
         */

        CFDictionaryRef dict = _CFCopyServerVersionDictionary();
        if (!dict) {
            dict = _CFCopySystemVersionDictionary();
        }
        if (!dict) {
            VLOG(1) << "Failed to get a version dictionary";
            return "<Unknown>";
        }

        CFStringRef str = CFStringCreateWithFormat(
                nullptr, nullptr, CFSTR("%@ %@"),
                CFDictionaryGetValue(dict, _kCFSystemVersionProductNameKey),
                CFDictionaryGetValue(dict, _kCFSystemVersionProductVersionKey));
        if (!str) {
            CFRelease(dict);
            VLOG(1) << "Failed to get a version string from a dictionary";
            return "<Unknown>";
        }
        int length = CFStringGetLength(str);
        if (!length) {
            CFRelease(str);
            CFRelease(dict);
            VLOG(1) << "Failed to get a version string length";
            return "<Unknown>";
        }
        std::string version(length, '\0');
        if (!CFStringGetCString(str, &version[0], version.size() + 1,
                                CFStringGetSystemEncoding())) {
            CFRelease(str);
            CFRelease(dict);
            VLOG(1) << "Failed to get a version string as C string";
            return "<Unknown>";
        }
        CFRelease(str);
        CFRelease(dict);
        lastSuccessfulValue = std::move(version);
        return lastSuccessfulValue;

#elif defined(__linux__)
        if (!lastSuccessfulValue.empty()) {
            return lastSuccessfulValue;
        }
        std::basic_stringbuf<char> std_out;
        auto proc =
                Command::Create({"lsb_release", "-d"}).RedirectStdoutToUnsafe(&std_out).Execute();

        if (proc->WaitFor(std::chrono::seconds(1)) != std::future_status::ready) {
            return "Unknown OS";
        }
        auto contents = proc->Out()->AsString();
        lastSuccessfulValue = absl::StripAsciiWhitespace(contents.substr(12, contents.size() - 12));
        return lastSuccessfulValue;
#else
#error getOsName(): unsupported OS;
#endif
    }

    std::string GetMajorOsVersion() const override {
        int majorVersion = 0, minorVersion = 0;
#ifdef _WIN32
        OSVERSIONINFOEXW ver;
        ver.dwOSVersionInfoSize = sizeof(ver);
        GetVersionExW((OSVERSIONINFOW*)&ver);
        majorVersion = ver.dwMajorVersion;
        minorVersion = ver.dwMinorVersion;
#else
        struct utsname name;
        if (uname(&name) == 0) {
            // Now parse out version numbers, as this will look something like:
            // 4.19.67-xxx or 18.6.0 or so.
            sscanf(name.release, "%d.%d", &majorVersion, &minorVersion);
        }
#endif
        return std::to_string(majorVersion) + "." + std::to_string(minorVersion);
    }

    int GetCpuCoreCount() const override {
#ifdef _WIN32
        SYSTEM_INFO si = {};
        ::GetSystemInfo(&si);
        return si.dwNumberOfProcessors < 1 ? 1 : si.dwNumberOfProcessors;
#else
        auto res = (int)::sysconf(_SC_NPROCESSORS_ONLN);
        return res < 1 ? 1 : res;
#endif
    }

    MemUsage GetMemUsage() const override {
        MemUsage res = {};
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX memCounters = {sizeof(memCounters)};

        if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                                   reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memCounters),
                                   sizeof(memCounters))) {
            uint64_t pageFileUsageCommit = memCounters.PagefileUsage ? memCounters.PagefileUsage
                                                                     : memCounters.PrivateUsage;

            res.resident = memCounters.WorkingSetSize;
            res.resident_max = memCounters.PeakWorkingSetSize;
            res.virt = pageFileUsageCommit;
            res.virt_max = memCounters.PeakPagefileUsage;
        }

        MEMORYSTATUSEX mem = {sizeof(mem)};
        if (::GlobalMemoryStatusEx(&mem)) {
            res.total_phys_memory = mem.ullTotalPhys;
            res.avail_phys_memory = mem.ullAvailPhys;
            res.total_page_file = mem.ullTotalPageFile;
        }
#elif defined(__linux__)
        size_t size = 0;
        std::ifstream fin;

        fin.open("/proc/self/status");
        if (!fin.good()) {
            return res;
        }

        std::string line;
        while (std::getline(fin, line)) {
            if (sscanf(line.c_str(), "VmRSS:%lu", &size) == 1) {
                res.resident = size * 1024;
            } else if (sscanf(line.c_str(), "VmHWM:%lu", &size) == 1) {
                res.resident_max = size * 1024;
            } else if (sscanf(line.c_str(), "VmSize:%lu", &size) == 1) {
                res.virt = size * 1024;
            } else if (sscanf(line.c_str(), "VmPeak:%lu", &size) == 1) {
                res.virt_max = size * 1024;
            }
        }
        fin.close();

        fin.open("/proc/meminfo");
        if (!fin.good()) {
            return res;
        }

        while (std::getline(fin, line)) {
            if (sscanf(line.c_str(), "MemTotal:%lu", &size) == 1) {
                res.total_phys_memory = size * 1024;
            } else if (sscanf(line.c_str(), "MemAvailable:%lu", &size) == 1) {
                res.avail_phys_memory = size * 1024;
            } else if (sscanf(line.c_str(), "SwapTotal:%lu", &size) == 1) {
                res.total_page_file = size * 1024;
            }
        }
        fin.close();

#elif defined(__APPLE__)
        mach_task_basic_info info = {};
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &infoCount);

        uint64_t total_phys = 0;
        {
            int mib[2] = {CTL_HW, HW_MEMSIZE};
            size_t len = sizeof(uint64_t);
            sysctl(mib, 2, &total_phys, &len, nullptr, 0);
        }

        res.resident = info.resident_size;
        res.resident_max = info.resident_size_max;
        res.virt = info.virtual_size;
        res.virt_max = 0;                    // Max virtual NYI for macOS
        res.total_phys_memory = total_phys;  // Max virtual NYI for macOS
        res.total_page_file = 0;             // Total page file NYI for macOS

        // Available memory detection: taken from the vm_stat utility sources.
        vm_size_t pageSize = 4096;
        const auto host = mach_host_self();
        host_page_size(host, &pageSize);
        vm_statistics64_data_t vm_stat;
        unsigned int count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) ==
            KERN_SUCCESS) {
            res.avail_phys_memory = (vm_stat.free_count - vm_stat.speculative_count) * pageSize;
        }
#endif
        return res;
    }

    std::string EnvGet(std::string_view varname) const override {
        return GetEnvironmentVariable(varname);
    }

    void EnvSet(const std::string& varname, const std::string& varvalue) override {
        SetEnvironmentVariable(varname, varvalue);
    }

    bool EnvTest(std::string_view varname) const override {
#ifdef _WIN32
        Win32UnicodeString varname_unicode(varname.data());
        const wchar_t* value = _wgetenv(varname_unicode.c_str());
        return value && value[0] != L'\0';
#else
        const char* value = getenv(c_str(varname));
        return value && value[0] != '\0';
#endif
    }

    std::vector<std::string> EnvGetAll() const override {
        std::vector<std::string> res;
        for (auto env = environ; env && *env; ++env) {
            res.push_back(*env);
        }
        return res;
    }

    bool IsRemoteSession(std::string* sessionType) const final {
        if (EnvTest("NX_TEMP")) {
            if (sessionType) {
                *sessionType = "NX";
            }
            return true;
        }
        if (EnvTest("CHROME_REMOTE_DESKTOP_SESSION")) {
            if (sessionType) {
                *sessionType = "Chrome Remote Desktop";
            }
            return true;
        }
        if (!EnvGet("SSH_CONNECTION").empty() && !EnvGet("SSH_CLIENT").empty()) {
            // This can be a remote X11 session, let's check if DISPLAY is set
            // to something uncommon.
            if (EnvGet("DISPLAY").size() > 2) {
                if (sessionType) {
                    *sessionType = "X11 Forwarding";
                }
                return true;
            }
        }

#ifdef _WIN32

        // https://docs.microsoft.com/en-us/windows/win32/termserv/detecting-the-terminal-services-environment
        //
        // "You should not use GetSystemMetrics(SM_REMOTESESSION) to determine if
        // your application is running in a remote session in Windows 8 and later or
        // Windows Server 2012 and later if the remote session may also be using the
        // RemoteFX vGPU improvements to the Microsoft Remote Display Protocol
        // (RDP). In this case, GetSystemMetrics(SM_REMOTESESSION) will identify the
        // remote session as a local session."

#define TERMINAL_SERVER_KEY "SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\"
#define GLASS_SESSION_ID "GlassSessionId"

        BOOL fIsRemoteable = FALSE;

        if (GetSystemMetrics(SM_REMOTESESSION)) {
            fIsRemoteable = TRUE;
        } else {
            HKEY hRegKey = NULL;
            LONG lResult;

            lResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY,
                                    0,  // ulOptions
                                    KEY_READ, &hRegKey);

            if (lResult == ERROR_SUCCESS) {
                DWORD dwGlassSessionId;
                DWORD cbGlassSessionId = sizeof(dwGlassSessionId);
                DWORD dwType;

                lResult = RegQueryValueExA(hRegKey, GLASS_SESSION_ID,
                                           NULL,  // lpReserved
                                           &dwType, (BYTE*)&dwGlassSessionId, &cbGlassSessionId);

                if (lResult == ERROR_SUCCESS) {
                    DWORD dwCurrentSessionId;
                    if (ProcessIdToSessionId(GetCurrentProcessId(), &dwCurrentSessionId)) {
                        fIsRemoteable = (dwCurrentSessionId != dwGlassSessionId);
                    }
                }
            }

            if (hRegKey) {
                RegCloseKey(hRegKey);
            }
        }

        if (TRUE == fIsRemoteable && sessionType) {
            *sessionType = "Windows Remote Desktop";
        }

        if (TRUE == fIsRemoteable) {
            return true;
        }

#endif  // _WIN32
        return false;
    }

    Times GetProcessTimes() const override {
        Times res = {};

#ifdef _WIN32
        FILETIME creationTime = {};
        FILETIME exitTime = {};
        FILETIME kernelTime = {};
        FILETIME userTime = {};
        ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime);

        // convert 100-ns intervals from a struct to int64_t milliseconds
        ULARGE_INTEGER kernelInt64;
        kernelInt64.LowPart = kernelTime.dwLowDateTime;
        kernelInt64.HighPart = kernelTime.dwHighDateTime;
        res.system_ms = static_cast<Duration>(kernelInt64.QuadPart / 10000);

        ULARGE_INTEGER userInt64;
        userInt64.LowPart = userTime.dwLowDateTime;
        userInt64.HighPart = userTime.dwHighDateTime;
        res.user_ms = static_cast<Duration>(userInt64.QuadPart / 10000);
#else
        tms times = {};
        ::times(&times);
        // convert to milliseconds
        const long int ticksPerSec = ::sysconf(_SC_CLK_TCK);
        res.system_ms = (times.tms_stime * 1000ll) / ticksPerSec;
        res.user_ms = (times.tms_utime * 1000ll) / ticksPerSec;
#endif
        res.wall_clock_ms = (kTickCount.getUs() - kTickCount.getStartTimeUs()) / 1000;

        return res;
    }

    time_t GetUnixTime() const override { return time(nullptr); }

    Duration GetUnixTimeUs() const override {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return tv.tv_sec * 1000000LL + tv.tv_usec;
    }

    WallDuration GetHighResTimeUs() const override { return kTickCount.getUs(); }

#ifdef _MSC_VER
    static void msvcInvalidParameterHandler(const wchar_t* expression, const wchar_t* function,
                                            const wchar_t* file, unsigned int line,
                                            uintptr_t pReserved) {
        // Don't expect too much from actually getting these parameters..
        std::wcerr << "Ignoring invalid parameter detected in function: " << function;
    }
#endif

    void ConfigureHost() const override {
#ifdef _MSC_VER
        _set_invalid_parameter_handler(msvcInvalidParameterHandler);
#endif
    }

    fs::path GetTempDir() const override {
#ifdef _WIN32
        Win32UnicodeString path(PATH_MAX);
        DWORD retval = GetTempPathW(path.size(), path.data());
        if (retval > path.size()) {
            path.resize(static_cast<size_t>(retval));
            retval = GetTempPathW(path.size(), path.data());
        }
        if (!retval) {
            // Best effort!
            return std::string("C:\\Temp");
        }
        path.resize(retval);
        // The result of GetTempPath() is already user-dependent
        // so don't append the username or userid to the result.
        path.append(L"\\AndroidEmulator");
        ::_wmkdir(path.c_str());
        return path.toString();
#else   // !_WIN32
        std::string result;
        const char* tmppath = getenv("ANDROID_TMP");
        if (!tmppath) {
            const char* user = getenv("USER");
            if (user == nullptr || user[0] == '\0') {
                user = "unknown";
            }
            result = "/tmp/android-";
            result += user;
        } else {
            result = tmppath;
        }

        fs::path tmp(result);
        if (!file::exists(tmp)) {
            file::mkdir_recursive(tmp, 744).IgnoreError();
        }

        if (!file::exists(tmp)) {
            LOG(FATAL) << "Failed to create: " << tmp;
        }
        return result;
#endif  // !_WIN32
    }

    bool GetEnableCrashReporting() const override {
        const bool defaultValue = true;

        const std::string enableCrashReporting = EnvGet("ANDROID_EMU_ENABLE_CRASH_REPORTING");

        if (enableCrashReporting.empty()) {
            return defaultValue;
        } else {
            LOG(INFO) << "Using crash reporting configuration from environment variable: "
                         "ANDROID_EMU_ENABLE_CRASH_REPORTING="
                      << enableCrashReporting;
            return parseBooleanValue(enableCrashReporting.c_str(), defaultValue);
        }
    }

  private:
    static void atexit_HostSystem();

    mutable fs::path mHomeDir;
    mutable fs::path mAppDataDir;
};

// HostSystem sHostSystem;
System* sSystemForTesting = nullptr;

// static
void HostSystem::atexit_HostSystem() {
    // do nothing..
}

}  // namespace

// static
System* System::Get() {
    System* result = sSystemForTesting;
    if (!result) {
        result = hostSystem();
    }
    return result;
}

#ifdef _WIN32
// static
const char* System::k_library_search_list_env_var_name = "PATH";
#elif defined(__APPLE__)
const char* System::k_library_search_list_env_var_name = "DYLD_LIBRARY_PATH";
#else
// static
const char* System::k_library_search_list_env_var_name = "LD_LIBRARY_PATH";
#endif

// static
System* System::SetForTesting(System* system) {
    System* result = sSystemForTesting;
    sSystemForTesting = system;
    return result;
}

System* System::hostSystem() {
    static absl::NoDestructor<HostSystem> sHostSystem;
    return sHostSystem.get();
}

// static
void System::AddLibrarySearchDir(fs::path path) {
    System* system = System::Get();
    const char* varName = k_library_search_list_env_var_name;

    std::string libSearchPath = system->EnvGet(varName);
    if (libSearchPath.size()) {
        libSearchPath = absl::StrFormat("%s%c%s", path.string(), kPathSeparator, libSearchPath);
    } else {
        libSearchPath = path.string();
    }
    LOG(INFO) << "Setting " << varName << " to " << libSearchPath;
    system->EnvSet(varName, libSearchPath);
}

// static
StorageCapacity System::FreeRamMb() {
    auto usage = Get()->GetMemUsage();
    return StorageCapacity(usage.avail_phys_memory, StorageCapacity::Unit::kB);
}

// static
bool System::IsUnderMemoryPressure(StorageCapacity* freeRamMb_out) {
    StorageCapacity currentFreeRam = FreeRamMb();

    if (freeRamMb_out) {
        *freeRamMb_out = currentFreeRam;
    }

    return currentFreeRam < kMemoryPressureLimit;
}

// static
System::FileSize System::GetFilePageSizeForPath(fs::path path) {
    System::FileSize pageSize;

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    // Use dwAllocationGranularity
    // as that is what we need to align
    // the pointer to (64k on most systems)
    pageSize = (System::FileSize)sysinfo.dwAllocationGranularity;
#else  // _WIN32

#ifdef __linux__

#define HUGETLBFS_MAGIC 0x958458f6

    struct statfs fsStatus;
    int ret;

    do {
        ret = statfs(path.c_str(), &fsStatus);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        VLOG(1) << "statvfs('" << path << "') failed: " << strerror(errno);
        pageSize = (System::FileSize)getpagesize();
    } else {
        if (fsStatus.f_type == HUGETLBFS_MAGIC) {
            LOG(INFO) << "hugepage detected. size:" << fsStatus.f_bsize;
            /* It's hugepage, return the huge page size */
            pageSize = (System::FileSize)fsStatus.f_bsize;
        } else {
            pageSize = (System::FileSize)getpagesize();
        }
    }
#else   // __linux
    pageSize = (System::FileSize)getpagesize();
#endif  // !__linux__

#endif  // !_WIN32

    return pageSize;
}

// static
void System::SetEnvironmentVariable(std::string_view varname, std::string_view varvalue) {
#ifdef _WIN32
    std::string envStr = absl::StrFormat("%s=%s", varname.data(), varvalue.data());
    // Note: this leaks the result of release().
    _wputenv(Win32UnicodeString(envStr).release());
#else
    if (varvalue.empty()) {
        unsetenv(c_str(varname));
    } else {
        setenv(c_str(varname), c_str(varvalue), 1);
    }
#endif
}

// static
std::string System::GetEnvironmentVariable(std::string_view varname) {
#ifdef _WIN32
    Win32UnicodeString varname_unicode(varname.data());
    const wchar_t* value = _wgetenv(varname_unicode.c_str());
    if (!value) {
        return std::string();
    } else {
        return Win32UnicodeString::convertToUtf8(value);
    }
#else
    const char* value = getenv(c_str(varname));
    if (!value) {
        value = "";
    }
    return std::string(value);
#endif
}

// static
System::WallDuration System::GetSystemTimeUs() {
    return kTickCount.getUs();
}

std::string toString(OsType osType) {
    switch (osType) {
    case OsType::kWindows:
        return "Windows";
    case OsType::kLinux:
        return "Linux";
    case OsType::kMac:
        return "Mac";
    default:
        return "Unknown";
    }
}

#ifdef __APPLE__
void disableAppNap_macImpl(void);
void cpuUsageCurrentThread_macImpl(uint64_t* user, uint64_t* sys);
#endif

// static
void System::DisableAppNap() {
#ifdef __APPLE__
    disableAppNap_macImpl();
#endif
}

// static
CpuTime System::cpuTime() {
    CpuTime res;

    res.wall_time_us = kTickCount.getUs();

#ifdef __APPLE__
    cpuUsageCurrentThread_macImpl(&res.user_time_us, &res.system_time_us);
#else

#ifdef __linux__
    struct rusage usage;
    getrusage(RUSAGE_THREAD, &usage);
    res.user_time_us = usage.ru_utime.tv_sec * 1000000ULL + usage.ru_utime.tv_usec;
    res.system_time_us = usage.ru_stime.tv_sec * 1000000ULL + usage.ru_stime.tv_usec;
#else  // Windows
    FILETIME creation_time_struct;
    FILETIME exit_time_struct;
    FILETIME kernel_time_struct;
    FILETIME user_time_struct;
    GetThreadTimes(GetCurrentThread(), &creation_time_struct, &exit_time_struct,
                   &kernel_time_struct, &user_time_struct);
    (void)creation_time_struct;
    (void)exit_time_struct;
    uint64_t user_time_100ns =
            user_time_struct.dwLowDateTime | ((uint64_t)user_time_struct.dwHighDateTime << 32);
    uint64_t system_time_100ns =
            kernel_time_struct.dwLowDateTime | ((uint64_t)kernel_time_struct.dwHighDateTime << 32);
    res.user_time_us = user_time_100ns / 10;
    res.system_time_us = system_time_100ns / 10;
#endif

#endif
    return res;
}

}  // namespace base
}  // namespace android

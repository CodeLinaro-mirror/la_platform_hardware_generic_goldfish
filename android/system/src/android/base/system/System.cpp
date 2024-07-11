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

#include "android/base/system/System.h"

#include "absl/strings//strip.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "aemu/base/EintrWrapper.h"
#include "aemu/base/files/ScopedFd.h"
#include "aemu/base/logging/CLog.h"
#include "aemu/base/logging/Log.h"
#include "aemu/base/memory/NoDestructor.h"
#include "aemu/base/memory/ScopedPtr.h"
#include "aemu/base/process/Command.h"
#include "aemu/base/system/System.h"
#include "android/base/system/CStrWrapper.h"
#include "android/base/system/storage_capacity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <inttypes.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include "aemu/base/files/ScopedFileHandle.h"
#include "aemu/base/files/ScopedRegKey.h"
#include "aemu/base/system/Win32UnicodeString.h"
#include "aemu/base/system/Win32Utils.h"
#include <ntddscsi.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <windows.h>
#include <winioctl.h>
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
} // extern "C"
#endif // __APPLE__

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/statvfs.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <time.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include "aemu/base/msvc.h"
#include "dirent.h"
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/vfs.h>

#include <fstream>
#include <string>
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
extern "C" char **environ;
#endif

#ifdef _WIN32
#if !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode)&S_IFMT) == S_IFDIR)
#endif
#if !defined(S_ISREG)
#define S_ISREG(mode) (((mode)&S_IFMT) == S_IFREG)
#endif
#endif

namespace android {
namespace base {
namespace fs = std::filesystem;

#ifdef __APPLE__
// Defined in system-native-mac.mm
std::optional<DiskKind> nativeDiskKind(int st_dev);
#endif


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
  long long mFreqPerSec = 0; // 0 means 'high perf counter isn't available'
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
#else // APPLE
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

} // namespace

namespace {

bool parseBooleanValue(const char *value, bool def) {
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
  HostSystem() : mProgramDir(), mHomeDir(), mAppDataDir() {
    ::atexit(HostSystem::atexit_HostSystem);
    configureHost();
  }

  ~HostSystem() override {}

  const fs::path getProgramDirectory() const override {
    if (mProgramDir.empty()) {
      mProgramDir.assign(getProgramDirectoryFromPlatform());
    }
    return mProgramDir;
  }

  fs::path getCurrentDirectory() const override {
#if defined(_WIN32)
    int currentLen = GetCurrentDirectoryW(0, nullptr);
    if (currentLen < 0) {
      // Could not get size of working directory. Something is really
      // fishy here, return an empty string.
      return std::string();
    }
    wchar_t *currentDir =
        static_cast<wchar_t *>(calloc(currentLen + 1, sizeof(wchar_t)));
    if (!GetCurrentDirectoryW(currentLen + 1, currentDir)) {
      // Again, some unexpected problem. Can't do much here.
      // Make the string empty.
      currentDir[0] = L'0';
    }

    std::string result = Win32UnicodeString::convertToUtf8(currentDir);
    ::free(currentDir);
    return result;
#else  // !_WIN32
    char currentDir[PATH_MAX];
    if (!getcwd(currentDir, sizeof(currentDir))) {
      return std::string();
    }
    return std::string(currentDir);
#endif // !_WIN32
  }

  bool setCurrentDirectory(fs::path directory) override {
    std::error_code err;
    fs::current_path(directory, err);
    return err.value() == 0;
  }

  const fs::path getLauncherDirectory() const override {
    if (mLauncherDir.empty()) {
      std::string launcherDirEnv = envGet("ANDROID_EMULATOR_LAUNCHER_DIR");
      if (!launcherDirEnv.empty()) {
        mLauncherDir = std::move(launcherDirEnv);
        return mLauncherDir;
      }
    }
    return mLauncherDir;
  }

  const fs::path getHomeDirectory() const override {
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
        std::string homedrive = envGet("HOMEDRIVE");
        std::string homepath = envGet("HOMEPATH");
        if (!homedrive.empty() && !homepath.empty()) {
          mHomeDir.assign(homedrive);
          mHomeDir.append(homepath);
        }
      }
#elif defined(__linux__) || (__APPLE__)
      // Try getting HOME from env first
      const char *home = getenv("HOME");
      if (home != nullptr) {
        mHomeDir.assign(home);
      } else {
        // If env HOME appears empty for some reason,
        // try getting HOME by querying system password database
        const struct passwd *pw = getpwuid(getuid());
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

  const fs::path getAppDataDirectory() const override {
#if defined(_WIN32)
    if (mAppDataDir.empty()) {
      // NOTE: See comment in getHomeDirectory().
      wchar_t path[MAX_PATH] = {0};
      if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        mAppDataDir = Win32UnicodeString::convertToUtf8(path);
      } else {
        const wchar_t *appdata = _wgetenv(L"APPDATA");
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
      mAppDataDir.assign(getHomeDirectory());
      mAppDataDir.append("/Library/Preferences");
    }
#elif defined(__linux__)
    ; // not applicable
#else
#error "Unsupported platform!"
#endif
    return mAppDataDir;
  }

  OsType getOsType() const override {
#ifdef _WIN32
    return OsType::Windows;
#elif defined(__APPLE__)
    return OsType::Mac;
#elif defined(__linux__)
    return OsType::Linux;
#else
#error getOsType(): unsupported OS;
#endif
  }

  std::string getOsName() override {
    static std::string lastSuccessfulValue;
    if (!lastSuccessfulValue.empty()) {
      return lastSuccessfulValue;
    }
#ifdef _WIN32
    using android::base::ScopedRegKey;
    HKEY hkey = 0;
    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hkey);
    if (result != ERROR_SUCCESS) {
      std::string errorStr =
          absl::StrFormat("Error: RegOpenKeyExA failed %ld %s", result,
                          Win32Utils::getErrorString(result));
      LOG(DEBUG) << errorStr;
      return errorStr;
    }
    ScopedRegKey hOsVersionKey(hkey);

    DWORD osNameSize = 0;
    const WCHAR productNameKey[] = L"ProductName";
    result = RegGetValueW(hOsVersionKey.get(), nullptr, productNameKey,
                          RRF_RT_REG_SZ, nullptr, nullptr, &osNameSize);
    if (result != ERROR_SUCCESS && ERROR_MORE_DATA != result) {
      std::string errorStr =
          absl::StrFormat("Error: RegGetValueW failed %ld %s", result,
                          Win32Utils::getErrorString(result));
      LOG(DEBUG) << errorStr;
      return errorStr;
    }

    Win32UnicodeString osName;
    osName.resize((osNameSize - 1) / sizeof(wchar_t));
    result = RegGetValueW(hOsVersionKey.get(), nullptr, productNameKey,
                          RRF_RT_REG_SZ, nullptr, osName.data(), &osNameSize);
    if (result != ERROR_SUCCESS) {
      std::string errorStr =
          absl::StrFormat("Error: RegGetValueW failed %ld %s", result,
                          Win32Utils::getErrorString(result));
      LOG(DEBUG) << errorStr;
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
      LOG(DEBUG) << "Failed to get a version dictionary";
      return "<Unknown>";
    }

    CFStringRef str = CFStringCreateWithFormat(
        nullptr, nullptr, CFSTR("%@ %@"),
        CFDictionaryGetValue(dict, _kCFSystemVersionProductNameKey),
        CFDictionaryGetValue(dict, _kCFSystemVersionProductVersionKey));
    if (!str) {
      CFRelease(dict);
      LOG(DEBUG) << "Failed to get a version string from a dictionary";
      return "<Unknown>";
    }
    int length = CFStringGetLength(str);
    if (!length) {
      CFRelease(str);
      CFRelease(dict);
      LOG(DEBUG) << "Failed to get a version string length";
      return "<Unknown>";
    }
    std::string version(length, '\0');
    if (!CFStringGetCString(str, &version[0], version.size() + 1,
                            CFStringGetSystemEncoding())) {
      CFRelease(str);
      CFRelease(dict);
      LOG(DEBUG) << "Failed to get a version string as C string";
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

    auto proc =
        Command::create({"lsb_release", "-d"}).withStdoutBuffer(4096).execute();

    if (proc->wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
      return "Unknown OS";
    }
    auto contents = proc->out()->asString();
    lastSuccessfulValue =
        absl::StripAsciiWhitespace(contents.substr(12, contents.size() - 12));
    return lastSuccessfulValue;
#else
#error getOsName(): unsupported OS;
#endif
  }

  std::string getMajorOsVersion() const override {
    int majorVersion = 0, minorVersion = 0;
#ifdef _WIN32
    OSVERSIONINFOEXW ver;
    ver.dwOSVersionInfoSize = sizeof(ver);
    GetVersionExW((OSVERSIONINFOW *)&ver);
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

  int getCpuCoreCount() const override {
#ifdef _WIN32
    SYSTEM_INFO si = {};
    ::GetSystemInfo(&si);
    return si.dwNumberOfProcessors < 1 ? 1 : si.dwNumberOfProcessors;
#else
    auto res = (int)::sysconf(_SC_NPROCESSORS_ONLN);
    return res < 1 ? 1 : res;
#endif
  }

  MemUsage getMemUsage() const override {
    MemUsage res = {};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memCounters = {sizeof(memCounters)};

    if (::GetProcessMemoryInfo(
            ::GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memCounters),
            sizeof(memCounters))) {
      uint64_t pageFileUsageCommit = memCounters.PagefileUsage
                                         ? memCounters.PagefileUsage
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
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
              reinterpret_cast<task_info_t>(&info), &infoCount);

    uint64_t total_phys = 0;
    {
      int mib[2] = {CTL_HW, HW_MEMSIZE};
      size_t len = sizeof(uint64_t);
      sysctl(mib, 2, &total_phys, &len, nullptr, 0);
    }

    res.resident = info.resident_size;
    res.resident_max = info.resident_size_max;
    res.virt = info.virtual_size;
    res.virt_max = 0;                   // Max virtual NYI for macOS
    res.total_phys_memory = total_phys; // Max virtual NYI for macOS
    res.total_page_file = 0;            // Total page file NYI for macOS

    // Available memory detection: taken from the vm_stat utility sources.
    vm_size_t pageSize = 4096;
    const auto host = mach_host_self();
    host_page_size(host, &pageSize);
    vm_statistics64_data_t vm_stat;
    unsigned int count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat,
                          &count) == KERN_SUCCESS) {
      res.avail_phys_memory =
          (vm_stat.free_count - vm_stat.speculative_count) * pageSize;
    }
#endif
    return res;
  }

  std::optional<DiskKind> pathDiskKind(fs::path path) override {
    return diskKindInternal(path);
  }
  std::optional<DiskKind> diskKind(int fd) override {
    return diskKindInternal(fd);
  }

  std::vector<fs::path> scanDirEntries(fs::path dirPath,
                                       bool fullPath = false) const override {
    auto result = scanDirInternal(dirPath);

    if (fullPath) {
      for (int i = 0; i < result.size(); i++) {
        result[i] = dirPath / result[i];
      }
    }
    return result;
  }

  std::string envGet(std::string_view varname) const override {
    return getEnvironmentVariable(varname);
  }

  void envSet(const std::string &varname,
              const std::string &varvalue) override {
    setEnvironmentVariable(varname, varvalue);
  }

  bool envTest(std::string_view varname) const override {
#ifdef _WIN32
    Win32UnicodeString varname_unicode(varname.data());
    const wchar_t *value = _wgetenv(varname_unicode.c_str());
    return value && value[0] != L'\0';
#else
    const char *value = getenv(c_str(varname));
    return value && value[0] != '\0';
#endif
  }

  std::vector<std::string> envGetAll() const override {
    std::vector<std::string> res;
    for (auto env = environ; env && *env; ++env) {
      res.push_back(*env);
    }
    return res;
  }

  bool isRemoteSession(std::string *sessionType) const final {
    if (envTest("NX_TEMP")) {
      if (sessionType) {
        *sessionType = "NX";
      }
      return true;
    }
    if (envTest("CHROME_REMOTE_DESKTOP_SESSION")) {
      if (sessionType) {
        *sessionType = "Chrome Remote Desktop";
      }
      return true;
    }
    if (!envGet("SSH_CONNECTION").empty() && !envGet("SSH_CLIENT").empty()) {
      // This can be a remote X11 session, let's check if DISPLAY is set
      // to something uncommon.
      if (envGet("DISPLAY").size() > 2) {
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

#define TERMINAL_SERVER_KEY                                                    \
  "SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\"
#define GLASS_SESSION_ID "GlassSessionId"

    BOOL fIsRemoteable = FALSE;

    if (GetSystemMetrics(SM_REMOTESESSION)) {
      fIsRemoteable = TRUE;
    } else {
      HKEY hRegKey = NULL;
      LONG lResult;

      lResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY,
                              0, // ulOptions
                              KEY_READ, &hRegKey);

      if (lResult == ERROR_SUCCESS) {
        DWORD dwGlassSessionId;
        DWORD cbGlassSessionId = sizeof(dwGlassSessionId);
        DWORD dwType;

        lResult = RegQueryValueExA(hRegKey, GLASS_SESSION_ID,
                                   NULL, // lpReserved
                                   &dwType, (BYTE *)&dwGlassSessionId,
                                   &cbGlassSessionId);

        if (lResult == ERROR_SUCCESS) {
          DWORD dwCurrentSessionId;
          if (ProcessIdToSessionId(GetCurrentProcessId(),
                                   &dwCurrentSessionId)) {
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

#endif // _WIN32
    return false;
  }

  bool pathExists(fs::path path) const override {
    return pathExistsInternal(path);
  }

  bool pathIsFile(fs::path path) const override {
    return pathIsFileInternal(path);
  }

  bool pathIsDir(fs::path path) const override {
    return pathIsDirInternal(path);
  }

  bool pathIsLink(fs::path path) const override {
    return pathIsLinkInternal(path);
  }

  bool pathCanRead(fs::path path) const override {
    return pathCanReadInternal(path);
  }

  bool pathCanWrite(fs::path path) const override {
    return pathCanWriteInternal(path);
  }

  bool pathCanExec(fs::path path) const override {
    return pathCanExecInternal(path);
  }

  bool pathIsQcow2(fs::path path) const override {
    return pathIsQcow2Internal(path);
  }

  bool pathFileSystemIsExt4(fs::path path) const override {
    return pathFileSystemIsExt4Internal(path);
  }

  bool pathIsExt4(fs::path path) const override {
    return pathIsExt4Internal(path);
  }

  int pathOpen(const char *filename, int oflag, int pmode) const override {
    return pathOpenInternal(filename, oflag, pmode);
  }

  bool deleteFile(fs::path path) const override {
    return deleteFileInternal(path);
  }

  bool pathFileSize(fs::path path, FileSize *outFileSize) const override {
    return pathFileSizeInternal(path, outFileSize);
  }

  FileSize recursiveSize(fs::path path) const override {
    return recursiveSizeInternal(path);
  }

  bool pathFreeSpace(fs::path path, FileSize *spaceInBytes) const override {
    return pathFreeSpaceInternal(path, spaceInBytes);
  }

  bool fileSize(int fd, FileSize *outFileSize) const override {
    return fileSizeInternal(fd, outFileSize);
  }
  std::optional<Duration> pathCreationTime(fs::path path) const override {
    return pathCreationTimeInternal(path);
  }

  std::optional<Duration> pathModificationTime(fs::path path) const override {
    return pathModificationTimeInternal(path);
  }

  Times getProcessTimes() const override {
    Times res = {};

#ifdef _WIN32
    FILETIME creationTime = {};
    FILETIME exitTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};
    ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime,
                      &kernelTime, &userTime);

    // convert 100-ns intervals from a struct to int64_t milliseconds
    ULARGE_INTEGER kernelInt64;
    kernelInt64.LowPart = kernelTime.dwLowDateTime;
    kernelInt64.HighPart = kernelTime.dwHighDateTime;
    res.systemMs = static_cast<Duration>(kernelInt64.QuadPart / 10000);

    ULARGE_INTEGER userInt64;
    userInt64.LowPart = userTime.dwLowDateTime;
    userInt64.HighPart = userTime.dwHighDateTime;
    res.userMs = static_cast<Duration>(userInt64.QuadPart / 10000);
#else
    tms times = {};
    ::times(&times);
    // convert to milliseconds
    const long int ticksPerSec = ::sysconf(_SC_CLK_TCK);
    res.systemMs = (times.tms_stime * 1000ll) / ticksPerSec;
    res.userMs = (times.tms_utime * 1000ll) / ticksPerSec;
#endif
    res.wallClockMs = (kTickCount.getUs() - kTickCount.getStartTimeUs()) / 1000;

    return res;
  }

  time_t getUnixTime() const override { return time(nullptr); }

  Duration getUnixTimeUs() const override {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
  }

  WallDuration getHighResTimeUs() const override { return kTickCount.getUs(); }
  void sleepMs(unsigned n) const override {
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
  }

  void sleepUs(unsigned n) const override {
    std::this_thread::sleep_for(std::chrono::microseconds(n));
  }

  void sleepToUs(WallDuration absTimeUs) const override {
    // Approach will vary based on platform.
    //
    // Linux has clock_nanosleep with TIMER_ABSTIME which does
    // exactly what we want, a sleep to some absolute time.
    //
    // Mac only has relative nanosleep(), so we'll need to calculate a time
    // difference.
    //
    // Windows has waitable timers. Pre Windows 10 1803, 1 ms was the best
    // resolution. Past that, we can use high resolution waitable timers.
#ifdef __APPLE__
    WallDuration current = getHighResTimeUs();

    // Already passed deadline, return.
    if (absTimeUs < current) {
      return;
    }
    WallDuration diff = absTimeUs - current;

    struct timespec ts;
    ts.tv_sec = diff / 1000000ULL;
    ts.tv_nsec = diff * 1000ULL - ts.tv_sec * 1000000000ULL;
    int ret;
    do {
      ret = nanosleep(&ts, nullptr);
    } while (ret == -1 && errno == EINTR);
#elif defined(__linux__)
    struct timespec ts;
    ts.tv_sec = absTimeUs / 1000000ULL;
    ts.tv_nsec = absTimeUs * 1000ULL - ts.tv_sec * 1000000000ULL;
    int ret;
    do {
      ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    } while (ret == -1 && errno == EINTR);
#else // _WIN32

    // Create a persistent thread local timer object
    struct ThreadLocalTimerState {
      ThreadLocalTimerState() {
        timerHandle = CreateWaitableTimerEx(
            nullptr /* no security attributes */, nullptr /* no timer name */,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

        if (!timerHandle) {
          // Use an older version of waitable timer as backup.
          timerHandle = CreateWaitableTimer(nullptr, FALSE, nullptr);
        }
      }

      ~ThreadLocalTimerState() {
        if (timerHandle) {
          CloseHandle(timerHandle);
        }
      }

      HANDLE timerHandle = 0;
    };

    static thread_local ThreadLocalTimerState tl_timerInfo;

    WallDuration current = getHighResTimeUs();
    // Already passed deadline, return.
    if (absTimeUs < current)
      return;
    WallDuration diff = absTimeUs - current;

    // Waitable Timer appraoch

    // We failed to create ANY usable timer. Sleep instead.
    if (!tl_timerInfo.timerHandle) {
      std::this_thread::sleep_for(std::chrono::microseconds(diff));
      return;
    }

    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -1LL * diff * 10LL; // 1 us = 1x 100ns
    SetWaitableTimer(tl_timerInfo.timerHandle, &dueTime, 0 /* one shot timer */,
                     0 /* no callback on finish */,
                     NULL /* no arg to completion routine */,
                     FALSE /* no suspend */);
    WaitForSingleObject(tl_timerInfo.timerHandle, INFINITE);
#endif
  }

  void yield() const override { std::this_thread::yield(); }

#ifdef _MSC_VER
  static void msvcInvalidParameterHandler(const wchar_t *expression,
                                          const wchar_t *function,
                                          const wchar_t *file,
                                          unsigned int line,
                                          uintptr_t pReserved) {
    // Don't expect too much from actually getting these parameters..
    std::wcerr << "Ignoring invalid parameter detected in function: "
               << function;
  }
#endif

  void configureHost() const override {
#ifdef _MSC_VER
    _set_invalid_parameter_handler(msvcInvalidParameterHandler);
#endif
  }

  fs::path getTempDir() const override {
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
#else  // !_WIN32
    std::string result;
    const char *tmppath = getenv("ANDROID_TMP");
    if (!tmppath) {
      const char *user = getenv("USER");
      if (user == nullptr || user[0] == '\0') {
        user = "unknown";
      }
      result = "/tmp/android-";
      result += user;
    } else {
      result = tmppath;
    }

    fs::path tmp(result);
    std::error_code ec;
    if (!fs::exists(tmp) && fs::create_directories(tmp, ec)) {
      fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write |
                               fs::perms::owner_exec | fs::perms::group_read |
                               fs::perms::others_read);
    }

    if (!fs::exists(tmp)) {
      dwarning("Failed to create: %s", tmp);
    }
    return result;
#endif // !_WIN32
  }

  bool getEnableCrashReporting() const override {
    const bool defaultValue = true;

    const std::string enableCrashReporting =
        envGet("ANDROID_EMU_ENABLE_CRASH_REPORTING");

    if (enableCrashReporting.empty()) {
      return defaultValue;
    } else {
      dinfo("Using crash reporting configuration from environment variable: "
            "ANDROID_EMU_ENABLE_CRASH_REPORTING=%s",
            enableCrashReporting);
      return parseBooleanValue(enableCrashReporting.c_str(), defaultValue);
    }
  }

private:
  static void atexit_HostSystem();

  mutable fs::path mProgramDir;
  mutable fs::path mLauncherDir;
  mutable fs::path mHomeDir;
  mutable fs::path mAppDataDir;
};

// HostSystem sHostSystem;
System *sSystemForTesting = nullptr;

// static
void HostSystem::atexit_HostSystem() {
  // do nothing..
}

#ifdef _WIN32
// Return |path| as a Unicode string, while discarding trailing separators.
Win32UnicodeString win32Path(fs::path path) {
  Win32UnicodeString wpath(path.string());
  // Get rid of trailing directory separators, Windows doesn't like them.
  size_t size = wpath.size();
  while (size > 0U && (wpath[size - 1U] == L'\\' || wpath[size - 1U] == L'/')) {
    size--;
  }
  if (size < wpath.size()) {
    wpath.resize(size);
  }
  return wpath;
}

using PathStat = struct _stat64;

#else // _WIN32

using PathStat = struct stat;

#endif // _WIN32

int pathStat(fs::path path, PathStat *st) {
#ifdef _WIN32
  return _wstat64(win32Path(path).c_str(), st);
#else  // !_WIN32
  return HANDLE_EINTR(stat(path.c_str(), st));
#endif // !_WIN32
}

int fdStat(int fd, PathStat *st) {
#ifdef _WIN32
  return _fstat64(fd, st);
#else  // !_WIN32
  return HANDLE_EINTR(fstat(fd, st));
#endif // !_WIN32
}

#ifdef _WIN32
static int GetWin32Mode(int mode) {
  // Convert |mode| to win32 permission bits.
  int win32mode = 0x0;

  if ((mode & R_OK) || (mode & X_OK)) {
    win32mode |= 0x4;
  }
  if (mode & W_OK) {
    win32mode |= 0x2;
  }

  return win32mode;
}
#endif

int pathAccess(fs::path path, int mode) {
#ifdef _WIN32
  return _waccess(path.c_str(), GetWin32Mode(mode));
#else  // !_WIN32
  return HANDLE_EINTR(access(path.c_str(), mode));
#endif // !_WIN32
}

} // namespace

// static
System *System::get() {
  System *result = sSystemForTesting;
  if (!result) {
    result = hostSystem();
  }
  return result;
}

// static
const char *System::kLibSubDir = "lib64";
// static
const char *System::kBinSubDir = "bin";

#ifdef _WIN32
// static
const char *System::kLibrarySearchListEnvVarName = "PATH";
#elif defined(__APPLE__)
const char *System::kLibrarySearchListEnvVarName = "DYLD_LIBRARY_PATH";
#else
// static
const char *System::kLibrarySearchListEnvVarName = "LD_LIBRARY_PATH";
#endif

// static
System *System::setForTesting(System *system) {
  System *result = sSystemForTesting;
  sSystemForTesting = system;
  return result;
}

System *System::hostSystem() {
  static android::base::NoDestructor<HostSystem> sHostSystem;
  return sHostSystem.get();
}

// static
std::vector<fs::path> System::scanDirInternal(fs::path dirPath) {
  std::vector<fs::path> result;

  if (dirPath.empty()) {
    dwarning("Empty path!");
    return result;
  }

#ifdef _WIN32
  auto root = dirPath / "*";
  Win32UnicodeString rootUnicode{root.string()};
  struct _wfinddata_t findData;
  intptr_t findIndex = _wfindfirst(rootUnicode.c_str(), &findData);
  if (findIndex >= 0) {
    do {
      const wchar_t *name = findData.name;
      if (wcscmp(name, L".") != 0 && wcscmp(name, L"..") != 0) {
        result.push_back(Win32UnicodeString::convertToUtf8(name));
      }
    } while (_wfindnext(findIndex, &findData) >= 0);
    _findclose(findIndex);
  }
#else  // !_WIN32
  DIR *dir = ::opendir(dirPath.c_str());
  if (dir) {
    for (;;) {
      struct dirent *entry = ::readdir(dir);
      if (!entry) {
        break;
      }
      const char *name = entry->d_name;
      if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        result.push_back(std::string(name));
      }
    }
    ::closedir(dir);
  }
#endif // !_WIN32
  std::sort(result.begin(), result.end());
  return result;
}

// static
bool System::pathIsLinkInternal(fs::path path) {
#ifdef _WIN32
  // Supposedly GetFileAttributes() and FindFirstFile()
  // can be used to detect symbolic links. In my tests,
  // a symbolic link looked exactly like a regular file.
  return false;
#else
  struct stat fileStatus;
  if (lstat(path.c_str(), &fileStatus)) {
    return false;
  }
  return S_ISLNK(fileStatus.st_mode);
#endif
}

// static
bool System::pathExistsInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  int ret = pathAccess(path, F_OK);
  return (ret == 0) || (errno != ENOENT);
}

// static
bool System::pathIsFileInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  PathStat st;
  int ret = pathStat(path, &st);
  if (ret < 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

// static
bool System::pathIsDirInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  PathStat st;
  int ret = pathStat(path, &st);
  if (ret < 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

// static
bool System::pathCanReadInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  return pathAccess(path, R_OK) == 0;
}

// static
bool System::pathCanWriteInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  return pathAccess(path, W_OK) == 0;
}

// static
bool System::pathCanExecInternal(fs::path path) {
  if (path.empty()) {
    return false;
  }
  return pathAccess(path, X_OK) == 0;
}

bool System::readSomeBytes(fs::path path, char *array, int pos, int size) {
  if (size <= 0 || !pathCanReadInternal(path)) {
    return false;
  }
  std::ifstream ifs(path, std::ios_base::binary);
  if (!ifs.good()) {
    return false;
  }

  if (pos > 0) {
    ifs.ignore(pos);
  }

  ifs.read(array, size);

  return true;
}

#if defined(__linux__)
static void get_all_ext4_mount_dirs(std::vector<fs::path> &alldirs) {
  static const char *proc_mounts = "/proc/self/mounts";
  std::ifstream testFile(proc_mounts);
  std::string line;

  while (getline(testFile, line)) {
    std::string device, dir, parttype;

    std::stringstream ss(line);

    ss >> device;
    ss >> dir;
    ss >> parttype;
    if (parttype == std::string("ext4")) {
      alldirs.push_back(dir);
    }
  }
}

static bool dir_contains_path(const fs::path &path, const fs::path &dir) {
  fs::path absolute_path = fs::absolute(path); // Get absolute path
  fs::path absolute_dir = fs::absolute(dir);   // Get absolute dir

  if (absolute_path.root_name() !=
      absolute_dir.root_name()) { // Check if on same drive
    return false;
  }

  // Iterate over directory components
  for (auto p = absolute_dir.begin(), q = absolute_path.begin();
       p != absolute_dir.end(); ++p, ++q) {
    if (q == absolute_path.end() || *p != *q) {
      return false; // Reached end of path or components differ
    }
  }
  return true; // All components of dir are present in path
}

#endif

bool System::pathFileSystemIsExt4Internal(fs::path path) {
#if defined(__linux__)
  std::vector<fs::path> mount_dirs;
  get_all_ext4_mount_dirs(mount_dirs);

  for (const auto &dir : mount_dirs) {
    if (dir_contains_path(dir, path.c_str())) {
      return true;
    }
  }
#endif
  return false;
}

// static
bool System::pathIsExt4Internal(fs::path path) {
  // read 2 bytes
  uint8_t magic[2] = {'\0'};

  if (!readSomeBytes(path, reinterpret_cast<char *>(magic), 1080,
                     sizeof(magic))) {
    return false;
  }
  bool matched2bytes = false;
  if (magic[0] == 0x53 && magic[1] == 0xEF) {
    matched2bytes = true;
  }

  return matched2bytes;
}

// static
bool System::pathIsQcow2Internal(fs::path path) {
  // read 4 bytes
  uint8_t magic[4] = {'\0'};
  if (!readSomeBytes(path, reinterpret_cast<char *>(magic), 0, sizeof(magic))) {
    return false;
  }

  bool matched4bytes = false;
  if (magic[0] == 'Q' && magic[1] == 'F' && magic[2] == 'I' &&
      magic[3] == static_cast<uint8_t>('\xfb')) {
    matched4bytes = true;
  }

  return matched4bytes;
}

fs::perms System::octalModeToPerms(int octalMode) {
  fs::perms mode = fs::perms::none;

  // Owner permissions
  mode |= (octalMode & 0400) ? fs::perms::owner_read : fs::perms::none;
  mode |= (octalMode & 0200) ? fs::perms::owner_write : fs::perms::none;
  mode |= (octalMode & 0100) ? fs::perms::owner_exec : fs::perms::none;

  // Group permissions
  mode |= (octalMode & 0040) ? fs::perms::group_read : fs::perms::none;
  mode |= (octalMode & 0020) ? fs::perms::group_write : fs::perms::none;
  mode |= (octalMode & 0010) ? fs::perms::group_exec : fs::perms::none;

  // Others permissions
  mode |= (octalMode & 0004) ? fs::perms::others_read : fs::perms::none;
  mode |= (octalMode & 0002) ? fs::perms::others_write : fs::perms::none;
  mode |= (octalMode & 0001) ? fs::perms::others_exec : fs::perms::none;

  return mode;
}

// static
int System::pathOpenInternal(const char *filename, int oflag, int pmode) {
#ifdef _WIN32
  return _wopen(win32Path(filename).c_str(), oflag, pmode);
#else  // !_WIN32
  return ::open(filename, oflag, pmode);
#endif // !_WIN32
}

bool System::deleteFileInternal(fs::path path) {
  if (!pathIsFileInternal(path)) {
    return false;
  }

#ifdef _WIN32
  Win32UnicodeString path_unicode(path.string());
  int remove_res = _wremove(path_unicode.c_str());
#else
  int remove_res = remove(path.c_str());
#endif

#ifdef _WIN32
  if (remove_res < 0) {
    // Windows sometimes just fails to delete a file
    // on the first try.
    // Sleep a little bit and try again here.
    System::get()->sleepMs(1);
    remove_res = _wremove(path_unicode.c_str());
  }
#endif

  if (remove_res != 0) {
    dprint("Failed to delete file [%s]", path.string());
  }

  return remove_res == 0;
}

bool System::pathFreeSpaceInternal(fs::path path, FileSize *spaceInBytes) {
#ifdef _WIN32
  ULARGE_INTEGER freeBytesAvailableToUser;
  bool result =
      GetDiskFreeSpaceEx(path.c_str(), &freeBytesAvailableToUser, NULL, NULL);
  if (!result) {
    return false;
  }
  *spaceInBytes = freeBytesAvailableToUser.QuadPart;
  return true;
#else
  struct statvfs fsStatus;
  int result = statvfs(path.c_str(), &fsStatus);
  if (result != 0) {
    return false;
  }
  // LOG(INFO) << "Got: " << fsStatus.f_frsize << ", " << fsStatus.f_bavail;
  // Available space is (block size) * (# free blocks)
  *spaceInBytes = ((FileSize)fsStatus.f_frsize) * fsStatus.f_bavail;
  return true;
#endif
}

// static
bool System::pathFileSizeInternal(fs::path path, FileSize *outFileSize) {
  if (path.empty() || !outFileSize) {
    return false;
  }
  PathStat st;
  int ret = pathStat(path, &st);
  if (ret < 0 || !S_ISREG(st.st_mode)) {
    return false;
  }
  // This is off_t on POSIX and a 32/64 bit integral type on windows based on
  // the host / compiler combination. We cast everything to 64 bit unsigned to
  // play safe.
  *outFileSize = static_cast<FileSize>(st.st_size);
  return true;
}

// static
System::FileSize System::recursiveSizeInternal(fs::path path) {
  std::vector<fs::path> fileList;
  fileList.push_back(path);

  FileSize totalSize = 0;
  while (!fileList.empty()) {
    const auto currentPath = std::move(fileList.back());
    fileList.pop_back();
    if (pathIsFileInternal(currentPath) || pathIsLinkInternal(currentPath)) {
      // Regular file or link. Return its size.
      FileSize theSize;
      if (pathFileSizeInternal(currentPath, &theSize)) {
        totalSize += theSize;
      }
    } else if (pathIsDirInternal(currentPath)) {
      // Directory. Add its contents to the list.
      std::vector<fs::path> includedFiles = scanDirInternal(currentPath);
      for (const auto &file : includedFiles) {
        fileList.push_back(currentPath / file);
      }
    }
  }
  return totalSize;
}

bool System::fileSizeInternal(int fd, System::FileSize *outFileSize) {
  if (fd < 0) {
    return false;
  }
  PathStat st;
  int ret = fdStat(fd, &st);
  if (ret < 0 || !S_ISREG(st.st_mode)) {
    return false;
  }
  // This is off_t on POSIX and a 32/64 bit integral type on windows based on
  // the host / compiler combination. We cast everything to 64 bit unsigned to
  // play safe.
  *outFileSize = static_cast<FileSize>(st.st_size);
  return true;
}

// static
std::optional<System::Duration>
System::pathCreationTimeInternal(fs::path path) {
#if defined(__linux__) ||                                                      \
    (defined(__APPLE__) && !defined(_DARWIN_FEATURE_64_BIT_INODE))
  // TODO(zyy@): read the creation time directly from the ext4 attribute
  // on Linux.
  return {};
#else
  PathStat st;
  if (pathStat(path, &st)) {
    return {};
  }
#ifdef _WIN32
  return st.st_ctime * 1000000ll;
#else  // APPLE
  return st.st_birthtimespec.tv_sec * 1000000ll +
         st.st_birthtimespec.tv_nsec / 1000;
#endif // WIN32 && APPLE
#endif // Linux
}

// static
std::optional<System::Duration>
System::pathModificationTimeInternal(fs::path path) {
  PathStat st;
  if (pathStat(path, &st)) {
    return {};
  }

#ifdef _WIN32
  return st.st_mtime * 1000000ll;
#elif defined(__linux__)
  return st.st_mtim.tv_sec * 1000000ll + st.st_mtim.tv_nsec / 1000;
#else // Darwin
  return st.st_mtimespec.tv_sec * 1000000ll + st.st_mtimespec.tv_nsec / 1000;
#endif
}

static std::optional<DiskKind> diskKind(const PathStat &st) {
#ifdef _WIN32

  auto volumeName = absl::StrFormat(R"(\\?\%c:)", 'A' + st.st_dev);
  ScopedFileHandle volume(::CreateFileA(volumeName.c_str(), 0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, 0, NULL));
  if (!volume.valid()) {
    return {};
  }

  VOLUME_DISK_EXTENTS volumeDiskExtents;
  DWORD bytesReturned = 0;
  if ((!::DeviceIoControl(volume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                          NULL, 0, &volumeDiskExtents,
                          sizeof(volumeDiskExtents), &bytesReturned, NULL) &&
       ::GetLastError() != ERROR_MORE_DATA) ||
      bytesReturned != sizeof(volumeDiskExtents)) {
    return {};
  }
  if (volumeDiskExtents.NumberOfDiskExtents < 1) {
    return {};
  }

  auto deviceName = absl::StrFormat(
      R"(\\?\PhysicalDrive%d)", int(volumeDiskExtents.Extents[0].DiskNumber));
  ScopedFileHandle device(::CreateFileA(deviceName.c_str(), 0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, 0, NULL));
  if (!device.valid()) {
    return {};
  }

  STORAGE_PROPERTY_QUERY spqTrim;
  spqTrim.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceTrimProperty;
  spqTrim.QueryType = PropertyStandardQuery;
  DEVICE_TRIM_DESCRIPTOR dtd = {0};
  if (::DeviceIoControl(device.get(), IOCTL_STORAGE_QUERY_PROPERTY, &spqTrim,
                        sizeof(spqTrim), &dtd, sizeof(dtd), &bytesReturned,
                        NULL) &&
      bytesReturned == sizeof(dtd)) {
    // Some SSDs don't support TRIM, so this can't be a sign of an HDD.
    if (dtd.TrimEnabled) {
      return DiskKind::Ssd;
    }
  }

  bytesReturned = 0;
  STORAGE_PROPERTY_QUERY spqSeekP;
  spqSeekP.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceSeekPenaltyProperty;
  spqSeekP.QueryType = PropertyStandardQuery;
  DEVICE_SEEK_PENALTY_DESCRIPTOR dspd = {0};
  if (::DeviceIoControl(device.get(), IOCTL_STORAGE_QUERY_PROPERTY, &spqSeekP,
                        sizeof(spqSeekP), &dspd, sizeof(dspd), &bytesReturned,
                        NULL) &&
      bytesReturned == sizeof(dspd)) {
    return dspd.IncursSeekPenalty ? DiskKind::Hdd : DiskKind::Ssd;
  }

  // TODO: figure out how to issue this query when not admin and not opening
  //  disk for write access.
#if 0
    bytesReturned = 0;

    // This struct isn't in the MinGW distribution headers.
    struct ATAIdentifyDeviceQuery {
        ATA_PASS_THROUGH_EX header;
        WORD data[256];
    };
    ATAIdentifyDeviceQuery id_query = {};
    id_query.header.Length = sizeof(id_query.header);
    id_query.header.AtaFlags = ATA_FLAGS_DATA_IN;
    id_query.header.DataTransferLength = sizeof(id_query.data);
    id_query.header.TimeOutValue = 5;  // Timeout in seconds
    id_query.header.DataBufferOffset =
            offsetof(ATAIdentifyDeviceQuery, data[0]);
    id_query.header.CurrentTaskFile[6] = 0xec;  // ATA IDENTIFY DEVICE
    if (::DeviceIoControl(device.get(), IOCTL_ATA_PASS_THROUGH, &id_query,
                          sizeof(id_query), &id_query, sizeof(id_query),
                          &bytesReturned, NULL) &&
        bytesReturned == sizeof(id_query)) {
        // Index of nominal media rotation rate
        // SOURCE:
        // http://www.t13.org/documents/UploadedDocuments/docs2009/d2015r1a-ATAATAPI_Command_Set_-_2_ACS-2.pdf
        //          7.18.7.81 Word 217
        // QUOTE: Word 217 indicates the nominal media rotation rate of the
        // device and is defined in table:
        //          Value           Description
        //          --------------------------------
        //          0000h           Rate not reported
        //          0001h           Non-rotating media (e.g., solid state
        //                          device)
        //          0002h-0400h     Reserved
        //          0401h-FFFEh     Nominal media rotation rate in rotations per
        //                          minute (rpm) (e.g., 7 200 rpm = 1C20h)
        //          FFFFh           Reserved
        unsigned rate = id_query.data[217];
        if (rate == 1) {
            return DiskKind::Ssd;
        } else if (rate >= 0x0401 && rate <= 0xFFFE) {
            return DiskKind::Hdd;
        }
    }
#endif

#elif defined __linux__

  // Parse /proc/partitions to find the corresponding device
  std::ifstream in("/proc/partitions");
  if (!in) {
    return {};
  }

  const auto maj = major(st.st_dev);
  const auto min = minor(st.st_dev);

  std::string line;
  std::string devName;

  std::unordered_set<std::string> devices;

  while (std::getline(in, line)) {
    unsigned curMaj, curMin;
    unsigned long blocks;
    char name[1024];
    if (sscanf(line.c_str(), "%u %u %lu %1023s", &curMaj, &curMin, &blocks,
               name) == 4) {
      devices.insert(name);
      if (curMaj == maj && curMin == min) {
        devName = name;
        break;
      }
    }
  }
  if (devName.empty()) {
    return {};
  }
  in.close();

  if (maj == 8) {
    // get rid of the partition number for block devices.
    while (!devName.empty() && isdigit(devName.back())) {
      devName.pop_back();
    }
    if (devices.find(devName) == devices.end()) {
      return {};
    }
  }

  // Now, having a device name, let's parse
  // /sys/block/%device%X/queue/rotational to get the result.
  auto sysPath = absl::StrFormat("/sys/block/%s/queue/rotational", devName);
  in.open(sysPath.c_str());
  if (!in) {
    return {};
  }
  char isRotational = 0;
  if (!(in >> isRotational)) {
    return {};
  }
  if (isRotational == '0') {
    return DiskKind::Ssd;
  } else if (isRotational == '1') {
    return DiskKind::Hdd;
  }

#else

  return nativeDiskKind(st.st_dev);

#endif

  // Sill got no idea.
  return {};
}

std::optional<DiskKind> System::diskKindInternal(fs::path path) {
  PathStat stat;
  if (pathStat(path, &stat)) {
    return {};
  }
  return android::base::diskKind(stat);
}

std::optional<DiskKind> System::diskKindInternal(int fd) {
  PathStat stat;
  if (fdStat(fd, &stat)) {
    return {};
  }
  return android::base::diskKind(stat);
}

// static
void System::addLibrarySearchDir(fs::path path) {
  System *system = System::get();
  const char *varName = kLibrarySearchListEnvVarName;

  std::string libSearchPath = system->envGet(varName);
  if (libSearchPath.size()) {
    libSearchPath = absl::StrFormat("%s%c%s", pathAsString(path),
                                    kPathSeparator, libSearchPath);
  } else {
    libSearchPath = pathAsString(path);
  }
  LOG(INFO) << "Setting " << varName << " to " << libSearchPath;
  system->envSet(varName, pathAsString(libSearchPath));
}

#ifndef _WIN32
const std::string kExe;
#else
const std::string kExe = ".exe";
#endif
// static
fs::path System::findBundledExecutable(std::string_view programName) {
  System *const system = System::get();
  const std::string executableName = std::string(programName) + kExe;
  fs::path executablePath = system->getLauncherDirectory() / executableName;

  if (system->pathIsFile(executablePath)) {
    return executablePath;
  }

  executablePath = system->getLauncherDirectory() / "bin" / executableName;
  if (system->pathIsFile(executablePath)) {
    return executablePath;
  }

  // We might be running in a bazel dev environment.. Make that work for now
  auto workspace = system->envGet("BUILD_WORKSPACE_DIRECTORY");
  if (workspace.empty()) {
    return "";
  }

  fs::path root = workspace;
  std::vector<fs::path> bazel_search{
      "bazel-bin/external/qemu",
      "bazel-bin/hardware/generic/goldfish/third_party/sparse"};

  for (const auto &option : bazel_search) {
    auto possible_exe = root / option / executableName;
    if (system->pathIsFile(possible_exe)) {
      return possible_exe;
    }
  }

  return "";
}

// static
StorageCapacity System::freeRamMb() {
  auto usage = get()->getMemUsage();
  return StorageCapacity(usage.avail_phys_memory, StorageCapacity::Unit::B);
}

// static
bool System::isUnderMemoryPressure(StorageCapacity *freeRamMb_out) {
  StorageCapacity currentFreeRam = freeRamMb();

  if (freeRamMb_out) {
    *freeRamMb_out = currentFreeRam;
  }

  return currentFreeRam < kMemoryPressureLimit;
}

// static
bool System::isUnderDiskPressure(fs::path path, System::FileSize *freeDisk) {
  System::FileSize availableSpace;
  bool success = System::get()->pathFreeSpace(path, &availableSpace);
  if (success && availableSpace < kDiskPressureLimit) {
    if (freeDisk) {
      *freeDisk = availableSpace;
    }
    return true;
  }

  return false;
}

// static
System::FileSize System::getFilePageSizeForPath(fs::path path) {
  System::FileSize pageSize;

#ifdef _WIN32
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  // Use dwAllocationGranularity
  // as that is what we need to align
  // the pointer to (64k on most systems)
  pageSize = (System::FileSize)sysinfo.dwAllocationGranularity;
#else // _WIN32

#ifdef __linux__

#define HUGETLBFS_MAGIC 0x958458f6

  struct statfs fsStatus;
  int ret;

  do {
    ret = statfs(path.c_str(), &fsStatus);
  } while (ret != 0 && errno == EINTR);

  if (ret != 0) {
    LOG(DEBUG) << "statvfs('" << path << "') failed: " << strerror(errno)
               << "\n";
    pageSize = (System::FileSize)getpagesize();
  } else {
    if (fsStatus.f_type == HUGETLBFS_MAGIC) {
      dinfo("hugepage detected. size: %lu", fsStatus.f_bsize);
      /* It's hugepage, return the huge page size */
      pageSize = (System::FileSize)fsStatus.f_bsize;
    } else {
      pageSize = (System::FileSize)getpagesize();
    }
  }
#else  // __linux
  pageSize = (System::FileSize)getpagesize();
#endif // !__linux__

#endif // !_WIN32

  return pageSize;
}

// static
void System::setEnvironmentVariable(std::string_view varname,
                                    std::string_view varvalue) {
#ifdef _WIN32
  std::string envStr =
      absl::StrFormat("%s=%s", varname.data(), varvalue.data());
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
std::string System::getEnvironmentVariable(std::string_view varname) {
#ifdef _WIN32
  Win32UnicodeString varname_unicode(varname.data());
  const wchar_t *value = _wgetenv(varname_unicode.c_str());
  if (!value) {
    return std::string();
  } else {
    return Win32UnicodeString::convertToUtf8(value);
  }
#else
  const char *value = getenv(c_str(varname));
  if (!value) {
    value = "";
  }
  return std::string(value);
#endif
}

// static
std::string System::getProgramDirectoryFromPlatform() {
  std::string res;
#if defined(__linux__)
  char path[1024];
  memset(path, 0, sizeof(path)); // happy valgrind!
  int len = readlink("/proc/self/exe", path, sizeof(path));
  if (len > 0 && len < (int)sizeof(path)) {
    char *x = ::strrchr(path, '/');
    if (x) {
      *x = '\0';
      res.assign(path);
    }
  }
#elif defined(__APPLE__)
  char s[PATH_MAX];
  auto pid = getpid();
  proc_pidpath(pid, s, sizeof(s));
  char *x = ::strrchr(s, '/');
  if (x) {
    // skip all slashes - there might be more than one
    while (x > s && x[-1] == '/') {
      --x;
    }
    *x = '\0';
    res.assign(s);
  } else {
    res.assign("<unknown-application-dir>");
  }
#elif defined(_WIN32)
  Win32UnicodeString appDir(PATH_MAX);
  int len = GetModuleFileNameW(0, appDir.data(), appDir.size());
  res.assign("<unknown-application-dir>");
  if (len > 0) {
    if (len > (int)appDir.size()) {
      appDir.resize(static_cast<size_t>(len));
      GetModuleFileNameW(0, appDir.data(), appDir.size());
    }
    std::string dir = appDir.toString();
    char *sep = ::strrchr(&dir[0], '\\');
    if (sep) {
      *sep = '\0';
      res.assign(dir.c_str());
    }
  }
#else
#error "Unsupported platform!"
#endif
  return res;
}

// static
System::WallDuration System::getSystemTimeUs() { return kTickCount.getUs(); }

std::string toString(OsType osType) {
  switch (osType) {
  case OsType::Windows:
    return "Windows";
  case OsType::Linux:
    return "Linux";
  case OsType::Mac:
    return "Mac";
  default:
    return "Unknown";
  }
}

#ifdef __APPLE__
// From
// http://mirror.informatimago.com/next/developer.apple.com/qa/qa2001/qa1123.html
typedef struct kinfo_proc kinfo_proc;

static int GetBSDProcessList(kinfo_proc **procList, size_t *procCount)
// Returns a list of all BSD processes on the system.  This routine
// allocates the list and puts it in *procList and a count of the
// number of entries in *procCount.  You are responsible for freeing
// this list (use "free" from System framework).
// On success, the function returns 0.
// On error, the function returns a BSD errno value.
{
  int err;
  kinfo_proc *result;
  bool done;
  static const int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
  // Declaring name as const requires us to cast it when passing it to
  // sysctl because the prototype doesn't include the const modifier.
  size_t length;

  assert(procList != NULL);
  assert(*procList == NULL);
  assert(procCount != NULL);

  *procCount = 0;

  // We start by calling sysctl with result == NULL and length == 0.
  // That will succeed, and set length to the appropriate length.
  // We then allocate a buffer of that size and call sysctl again
  // with that buffer.  If that succeeds, we're done.  If that fails
  // with ENOMEM, we have to throw away our buffer and loop.  Note
  // that the loop causes use to call sysctl with NULL again; this
  // is necessary because the ENOMEM failure case sets length to
  // the amount of data returned, not the amount of data that
  // could have been returned.

  result = nullptr;
  done = false;
  do {
    assert(result == NULL);

    // Call sysctl with a NULL buffer.

    length = 0;
    err = sysctl((int *)name, (sizeof(name) / sizeof(*name)) - 1, nullptr,
                 &length, nullptr, 0);
    if (err == -1) {
      err = errno;
    }

    // Allocate an appropriately sized buffer based on the results
    // from the previous call.

    if (err == 0) {
      result = (kinfo_proc *)malloc(length);
      if (result == nullptr) {
        err = ENOMEM;
      }
    }

    // Call sysctl again with the new buffer.  If we get an ENOMEM
    // error, toss away our buffer and start again.

    if (err == 0) {
      err = sysctl((int *)name, (sizeof(name) / sizeof(*name)) - 1, result,
                   &length, nullptr, 0);
      if (err == -1) {
        err = errno;
      }
      if (err == 0) {
        done = true;
      } else if (err == ENOMEM) {
        assert(result != NULL);
        free(result);
        result = nullptr;
        err = 0;
      }
    }
  } while (err == 0 && !done);

  // Clean up and establish post conditions.

  if (err != 0 && result != nullptr) {
    free(result);
    result = nullptr;
  }
  *procList = result;
  if (err == 0) {
    *procCount = length / sizeof(kinfo_proc);
  }

  assert((err == 0) == (*procList != NULL));

  return err;
}

// From
// https://astojanov.wordpress.com/2011/11/16/mac-os-x-resolve-absolute-path-using-process-pid/
std::optional<std::string> getPathOfProcessByPid(pid_t pid) {
  int ret;
  std::string result(PROC_PIDPATHINFO_MAXSIZE + 1, 0);
  ret = proc_pidpath(pid, (void *)result.data(), PROC_PIDPATHINFO_MAXSIZE);

  if (ret <= 0) {
    return {};
  } else {
    return result;
  }
}

#endif

static bool sMultiStringMatch(std::string_view haystack,
                              const std::vector<std::string_view> &needles,
                              bool approxMatch) {
  bool found = false;

  for (auto needle : needles) {
    found = found || approxMatch ? (absl::StrContains(haystack, needle))
                                 : (haystack == needle);
  }

  return found;
}

#ifdef __APPLE__
void disableAppNap_macImpl(void);
void cpuUsageCurrentThread_macImpl(uint64_t *user, uint64_t *sys);
#endif

// static
void System::disableAppNap() {
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
  res.user_time_us =
      usage.ru_utime.tv_sec * 1000000ULL + usage.ru_utime.tv_usec;
  res.system_time_us =
      usage.ru_stime.tv_sec * 1000000ULL + usage.ru_stime.tv_usec;
#else // Windows
  FILETIME creation_time_struct;
  FILETIME exit_time_struct;
  FILETIME kernel_time_struct;
  FILETIME user_time_struct;
  GetThreadTimes(GetCurrentThread(), &creation_time_struct, &exit_time_struct,
                 &kernel_time_struct, &user_time_struct);
  (void)creation_time_struct;
  (void)exit_time_struct;
  uint64_t user_time_100ns = user_time_struct.dwLowDateTime |
                             ((uint64_t)user_time_struct.dwHighDateTime << 32);
  uint64_t system_time_100ns =
      kernel_time_struct.dwLowDateTime |
      ((uint64_t)kernel_time_struct.dwHighDateTime << 32);
  res.user_time_us = user_time_100ns / 10;
  res.system_time_us = system_time_100ns / 10;
#endif

#endif
  return res;
}

#ifndef _WIN32

bool System::queryFileVersionInfo(fs::path, int *, int *, int *, int *) {
  return false;
}

#endif // _WIN32

} // namespace base
} // namespace android

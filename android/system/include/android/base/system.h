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

#pragma once

#include <limits.h>
#include <stdint.h>
#include <time.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "android/base/cpu_time.h"
#include "android/base/memory.h"
#include "android/base/storage_capacity.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#undef ERROR  // necessary to compile LOG(ERROR) statements
#else         // !_WIN32
#ifndef _MSC_VER
#include <unistd.h>
#endif
#endif  // !_WIN32

namespace android {
namespace base {

// Type of the current operating system
enum class OsType { kWindows, kMac, kLinux };

namespace fs = std::filesystem;
std::string ToString(OsType os_type);

enum class RunOptions {
    // Can't use None here: X.h defines None to 0L.
    kEmpty = 0,

    // some pseudo flags to just state the default behavior
    kDontWait = 0,
    kHideAllOutput = 0,

    // Wait for the launched shell command to finish, and return true only if
    // the command was successful.
    kWaitForCompletion = 1,
    // Attempt to terminate the launched process if it doesn't finish in time.
    // Note that terminating a mid-flight process can leave the whole system in
    // a weird state.
    // Only make sense with |WaitForCompletion|.
    kTerminateOnTimeout = 2,

    // These flags and RunOptions::HideAllOutput are mutually exclusive.
    kShowOutput = 4,
    kDumpOutputToFile = 8,

    kDefault = 0,  // don't wait, hide all output
};

// Interface class to the underlying operating system.
class System {
  public:
    typedef int64_t Duration;
    typedef uint64_t WallDuration;
    using FileSize = StorageCapacity;

    // Information about user, system and wall clock times for some process,
    // in milliseconds
    struct Times {
        Duration user_ms;
        Duration system_ms;
        WallDuration wall_clock_ms;
    };

    // Call this function to get the instance
    static System* Get();

    // Default constructor doesn't do anything.
    System() = default;

    // Default destructor is empty but virtual.
    virtual ~System() = default;

    System(const System&) = delete;
    System(System&&) = delete;
    System operator=(const System&) = delete;
    System operator=(System&&) = delete;

    // Return the current OS type
    virtual OsType GetOsType() const = 0;

    // Return the current OS product/version name.
    // Return error string in the format of "Error: [reason]"
    // if we are unable to get host OS information or error
    // occurs.
    virtual std::string GetOsName() = 0;

    // The major os version that this code is running on in
    // the form of '[0-9]+\.[0-9]+'
    virtual std::string GetMajorOsVersion() const = 0;

    // Get the number of hardware CPU cores available. Hyperthreading cores are
    // counted as separate here.
    virtual int GetCpuCoreCount() const = 0;

    virtual MemUsage GetMemUsage() const = 0;

    // Returns just the free RAM on the system. Useful in many cases.
    static StorageCapacity FreeRamMb();

    // Measures whether or not the system is considered in a memory pressure
    // state, and returns true if so. std::optionally, a freeRamMb output pointer
    // can be given so the caller can see how much RAM is actually free.
    static constexpr StorageCapacity kMemoryPressureLimit = 513_MiB;
    static bool IsUnderMemoryPressure(StorageCapacity* free_ram_mb = nullptr);

    static System::FileSize GetFilePageSizeForPath(fs::path path);

    // Environment variable name corresponding to the library search
    // list for shared libraries.
    static const char* k_library_search_list_env_var_name;

    // Return program's bitness, either 32 or 64.
    static int GetProgramBitness() { return 64; }

    // /////////////////////////////////////////////////////////////////////////
    // Environment variables.
    // /////////////////////////////////////////////////////////////////////////

    // Retrieve the value of a given environment variable.
    // Equivalent to getenv() but returns a std::string instance.
    // If the variable is not defined, return an empty string.
    // NOTE: On Windows, this uses _wgetenv() and returns the corresponding
    // UTF-8 text string.
    virtual std::string EnvGet(std::string_view varname) const = 0;

    // Set the value of a given environment variable.
    // If |varvalue| is NULL or empty, this unsets the variable.
    // Equivalent to setenv().
    virtual void EnvSet(const std::string& varname, const std::string& varvalue) = 0;

    virtual void EnvSet(const char* varname, const char* varvalue) final {
        if (!varname) {
            return;
        }
        EnvSet(std::string(varname), varvalue ? std::string(varvalue) : "");
    }
    // Returns true if environment variable |varname| is set and non-empty.
    virtual bool EnvTest(std::string_view varname) const = 0;

    // Returns all environment variables from the current process in a
    // "name=value" form
    virtual std::vector<std::string> EnvGetAll() const = 0;

    // Prepend a new directory to the system's library search path. This
    // only alters an environment variable like PATH or LD_LIBRARY_PATH,
    // and thus typically takes effect only after spawning/executing a new
    // process.
    static void AddLibrarySearchDir(fs::path path);

    // Return the path to user's home directory (as defined in the
    // underlying platform) or an empty string if it can't be found
    virtual const fs::path GetHomeDirectory() const = 0;

    // Return the path to user's App Data directory (only applies
    // in Microsoft Windows) or an empty string if it can't be found
    virtual const fs::path GetAppDataDirectory() const = 0;

    // Return if enable the crash reporting
    virtual bool GetEnableCrashReporting() const = 0;

    // /////////////////////////////////////////////////////////////////////////
    // Time related functions.
    // /////////////////////////////////////////////////////////////////////////

    // Checks the system to see if it is running under a remoting session
    // like Nomachine's NX, Chrome Remote Desktop or Windows Terminal Services.
    // On success, return true and sets |*sessionType| to the detected
    // session type. Otherwise, just return false.
    virtual bool IsRemoteSession(std::string* session_type) const = 0;

    // Returns Times structure for the current process
    virtual Times GetProcessTimes() const = 0;

    // Returns the current Unix timestamp
    virtual time_t GetUnixTime() const = 0;

    // Returns the current Unix timestamp with microsecond resolution
    virtual Duration GetUnixTimeUs() const = 0;

    // Returns the OS-specific high resolution timestamp.
    virtual WallDuration GetHighResTimeUs() const = 0;

    // Setup system specific handlers. For example on msvc you might
    // want to redirect parameter validation.
    virtual void ConfigureHost() const = 0;

    // bug: 117923532
    // macOS will make the emulator nap, which will mess up timers
    // and cause mayhem like hang detection.
    // On other platforms, this function doesn't do anything.
    static void DisableAppNap();

    // Returns the wallclock (high res time us) user, and system time spent
    // in the current thread.
    static CpuTime cpuTime();  // NOLINT

    // Static version that sets or queries host environment variables
    // regardless of being TestSystem.
    static void SetEnvironmentVariable(std::string_view varname, std::string_view varvalue);
    static std::string GetEnvironmentVariable(std::string_view varname);
    static WallDuration GetSystemTimeUs();

    // Return the path of a temporary directory appropriate for the system.
    virtual fs::path GetTempDir() const = 0;

  protected:
    size_t memory_size_ = 0;

    static System* SetForTesting(System* system);
    static System* hostSystem();  // NOLINT

    // Internal implementation of scanDirEntries() that can be used by
    // mock implementation using a fake file system rooted into a temporary
    // directory or something like that. Always returns short paths.
    static std::vector<fs::path> ScanDirInternal(fs::path dir_path);

    static bool ReadSomeBytes(fs::path path, char* array, int pos, int size);
};

}  // namespace base
}  // namespace android

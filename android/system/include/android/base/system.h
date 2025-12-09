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

#include "aemu/base/Compiler.h"
#include "aemu/base/CpuTime.h"
#include "aemu/base/EnumFlags.h"
#include "aemu/base/system/Memory.h"
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
enum class OsType { Windows, Mac, Linux };

namespace fs = std::filesystem;
std::string toString(OsType osType);

enum class RunOptions {
    // Can't use None here: X.h defines None to 0L.
    Empty = 0,

    // some pseudo flags to just state the default behavior
    DontWait = 0,
    HideAllOutput = 0,

    // Wait for the launched shell command to finish, and return true only if
    // the command was successful.
    WaitForCompletion = 1,
    // Attempt to terminate the launched process if it doesn't finish in time.
    // Note that terminating a mid-flight process can leave the whole system in
    // a weird state.
    // Only make sense with |WaitForCompletion|.
    TerminateOnTimeout = 2,

    // These flags and RunOptions::HideAllOutput are mutually exclusive.
    ShowOutput = 4,
    DumpOutputToFile = 8,

    Default = 0,  // don't wait, hide all output
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
        Duration userMs;
        Duration systemMs;
        WallDuration wallClockMs;
    };

  public:
    // Call this function to get the instance
    static System* get();

    // Default constructor doesn't do anything.
    System() = default;

    // Default destructor is empty but virtual.
    virtual ~System() = default;

    // Return the current OS type
    virtual OsType getOsType() const = 0;

    // Return the current OS product/version name.
    // Return error string in the format of "Error: [reason]"
    // if we are unable to get host OS information or error
    // occurs.
    virtual std::string getOsName() = 0;

    // The major os version that this code is running on in
    // the form of '[0-9]+\.[0-9]+'
    virtual std::string getMajorOsVersion() const = 0;

    // Get the number of hardware CPU cores available. Hyperthreading cores are
    // counted as separate here.
    virtual int getCpuCoreCount() const = 0;

    virtual MemUsage getMemUsage() const = 0;

    // Returns just the free RAM on the system. Useful in many cases.
    static StorageCapacity freeRamMb();

    // Measures whether or not the system is considered in a memory pressure
    // state, and returns true if so. std::optionally, a freeRamMb output pointer
    // can be given so the caller can see how much RAM is actually free.
    static constexpr StorageCapacity kMemoryPressureLimit = 513_MiB;
    static bool isUnderMemoryPressure(StorageCapacity* freeRamMb = nullptr);

    static System::FileSize getFilePageSizeForPath(fs::path path);

    // Environment variable name corresponding to the library search
    // list for shared libraries.
    static const char* kLibrarySearchListEnvVarName;

    // Return program's bitness, either 32 or 64.
    static int getProgramBitness() { return 64; }

    // /////////////////////////////////////////////////////////////////////////
    // Environment variables.
    // /////////////////////////////////////////////////////////////////////////

    // Retrieve the value of a given environment variable.
    // Equivalent to getenv() but returns a std::string instance.
    // If the variable is not defined, return an empty string.
    // NOTE: On Windows, this uses _wgetenv() and returns the corresponding
    // UTF-8 text string.
    virtual std::string envGet(std::string_view varname) const = 0;

    // Set the value of a given environment variable.
    // If |varvalue| is NULL or empty, this unsets the variable.
    // Equivalent to setenv().
    virtual void envSet(const std::string& varname, const std::string& varvalue) = 0;

    virtual void envSet(const char* varname, const char* varvalue) final {
        if (!varname) {
            return;
        }
        envSet(std::string(varname), varvalue ? std::string(varvalue) : "");
    }
    // Returns true if environment variable |varname| is set and non-empty.
    virtual bool envTest(std::string_view varname) const = 0;

    // Returns all environment variables from the current process in a
    // "name=value" form
    virtual std::vector<std::string> envGetAll() const = 0;

    // Prepend a new directory to the system's library search path. This
    // only alters an environment variable like PATH or LD_LIBRARY_PATH,
    // and thus typically takes effect only after spawning/executing a new
    // process.
    static void addLibrarySearchDir(fs::path path);

    // Return the path to user's home directory (as defined in the
    // underlying platform) or an empty string if it can't be found
    virtual const fs::path getHomeDirectory() const = 0;

    // Return the path to user's App Data directory (only applies
    // in Microsoft Windows) or an empty string if it can't be found
    virtual const fs::path getAppDataDirectory() const = 0;

    // Return if enable the crash reporting
    virtual bool getEnableCrashReporting() const = 0;

    // /////////////////////////////////////////////////////////////////////////
    // Time related functions.
    // /////////////////////////////////////////////////////////////////////////

    // Checks the system to see if it is running under a remoting session
    // like Nomachine's NX, Chrome Remote Desktop or Windows Terminal Services.
    // On success, return true and sets |*sessionType| to the detected
    // session type. Otherwise, just return false.
    virtual bool isRemoteSession(std::string* sessionType) const = 0;

    // Returns Times structure for the current process
    virtual Times getProcessTimes() const = 0;

    // Returns the current Unix timestamp
    virtual time_t getUnixTime() const = 0;

    // Returns the current Unix timestamp with microsecond resolution
    virtual Duration getUnixTimeUs() const = 0;

    // Returns the OS-specific high resolution timestamp.
    virtual WallDuration getHighResTimeUs() const = 0;

    // Sleep for |n| milliseconds
    virtual void sleepMs(unsigned n) const = 0;

    // Sleep for |n| microseconds
    virtual void sleepUs(unsigned n) const = 0;

    // Sleep to the specified WallDuration from getHighResTimeUs().
    virtual void sleepToUs(WallDuration absTimeUs) const = 0;

    // Yield the remaining part of current thread's CPU time slice to another
    // thread that's ready to run.
    virtual void yield() const = 0;

    // Setup system specific handlers. For example on msvc you might
    // want to redirect parameter validation.
    virtual void configureHost() const = 0;

    // bug: 117923532
    // macOS will make the emulator nap, which will mess up timers
    // and cause mayhem like hang detection.
    // On other platforms, this function doesn't do anything.
    static void disableAppNap();

    // Returns the wallclock (high res time us) user, and system time spent
    // in the current thread.
    static CpuTime cpuTime();

    // Static version that sets or queries host environment variables
    // regardless of being TestSystem.
    static void setEnvironmentVariable(std::string_view varname, std::string_view varvalue);
    static std::string getEnvironmentVariable(std::string_view varname);
    static WallDuration getSystemTimeUs();

    // Return the path of a temporary directory appropriate for the system.
    virtual fs::path getTempDir() const = 0;

  protected:
    size_t mMemorySize = 0;

    static System* setForTesting(System* system);
    static System* hostSystem();

    // Internal implementation of scanDirEntries() that can be used by
    // mock implementation using a fake file system rooted into a temporary
    // directory or something like that. Always returns short paths.
    static std::vector<fs::path> scanDirInternal(fs::path dirPath);

    static bool readSomeBytes(fs::path path, char* array, int pos, int size);

  private:
    DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace base
}  // namespace android

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

#include "absl/log/log.h"

#include "aemu/base/Compiler.h"
#include "aemu/base/CpuTime.h"
#include "aemu/base/EnumFlags.h"
#include "aemu/base/system/Memory.h"
#include "android/base/system/storage_capacity.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

#include "aemu/base/system/Win32UnicodeString.h"
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

    static constexpr StorageCapacity kDiskPressureLimit = 2_MiB;
    static bool isUnderDiskPressure(fs::path path, System::FileSize* freeDisk = nullptr);

    static System::FileSize getFilePageSizeForPath(fs::path path);

    inline static std::string pathAsString(const std::filesystem::path& path) {
#ifdef _WIN32
        return Win32UnicodeString(path.string().data(), path.string().size()).toString();
#else
        return path.string();
#endif
    }

    // Environment variable name corresponding to the library search
    // list for shared libraries.
    static const char* kLibrarySearchListEnvVarName;

    // Return the name of the sub-directory containing libraries
    // for the current platform, i.e. "lib" or "lib64" depending
    // on the value of kProgramBitness.
    static const char* kLibSubDir;

    // Return the name of the sub-directory containing executables
    // for the current platform, i.e. "bin" or "bin64" depending
    // on the value of kProgramBitness.
    static const char* kBinSubDir;

    // Name of the 32-bit binaries subdirectory
    static const char* kBin32SubDir;

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

    // /////////////////////////////////////////////////////////////////////////
    // Path functions that interact with the file system.
    //     Pure path manipulation functions are in android::base::PathUtils.
    // /////////////////////////////////////////////////////////////////////////

    // Return true iff |path| exists on the file system.
    virtual bool pathExists(fs::path path) const = 0;

    // Return true iff |path| exists and is a regular file on the file system.
    virtual bool pathIsFile(fs::path path) const = 0;

    // Return true iff |path| exists and is a directory on the file system.
    virtual bool pathIsDir(fs::path path) const = 0;

    // Return true iff |path| exists and is a symbolic link the file system.
    virtual bool pathIsLink(fs::path path) const = 0;

    // Return true iff |path| exists and can be read by the current user.
    virtual bool pathCanRead(fs::path path) const = 0;

    // Return true iff |path| exists and can be written to by the current
    // user.
    virtual bool pathCanWrite(fs::path path) const = 0;

    // Return true iff |path| exists and is qcow2 file
    // user.
    virtual bool pathIsQcow2(fs::path path) const = 0;

    // Return true iff |path| exists and is qcow2 file
    // user.
    virtual bool pathIsExt4(fs::path path) const = 0;

    // Return true iff |path| exists and can be executed to by the current
    // user.
    virtual bool pathCanExec(fs::path path) const = 0;

    // A wrapper for int open(filename, oflag, pmode) to support unicode paths
    // on Windows.
    virtual int pathOpen(const char* filename, int oflag, int pmode) const = 0;

    // Function for deleting files. Return true iff
    // (|path| is a file and we have successfully deleted it)
    virtual bool deleteFile(fs::path path) const = 0;

    // Get the size of file at |path|.
    // Fails if path is not a file or not readable, and in case of other errors.
    virtual bool pathFileSize(fs::path path, FileSize* outFileSize) const = 0;
    virtual bool fileSize(int fd, FileSize* outFileSize) const = 0;
    std::optional<FileSize> pathFileSize(fs::path path) {
        FileSize res;
        return pathFileSize(path, &res) ? std::make_optional(res) : std::nullopt;
    }
    std::optional<FileSize> fileSize(int fd) {
        FileSize res;
        return fileSize(fd, &res) ? std::make_optional(res) : std::nullopt;
    }

    // Get the size of the directory at |path|. Include all files
    // and subdirectories, recursively.
    // If |path| is a regular file, return the size of that file.
    virtual FileSize recursiveSize(fs::path path) const = 0;

    // Get the amount of free disk space, in bytes, at |path|.
    // Returns 'false' on error.
    virtual bool pathFreeSpace(fs::path path, FileSize* spaceInBytes) const = 0;

    // Gets the file creation timestamp as a Unix epoch time with microsecond
    // resolution. Returns an empty std::optional for systems that don't support
    // creation times (Linux) or if the operation failed.
    virtual std::optional<Duration> pathCreationTime(fs::path path) const = 0;

    // Gets the file modification timestamp as a Unix epoch time with
    // microsecond resolution. Returns an empty std::optional for systems that
    // don't support modification times or if the operation failed.
    virtual std::optional<Duration> pathModificationTime(fs::path path) const = 0;

    // whether the path is on ext4 filesystem, instead of btrfs, xfs etc
    // mainly for linux, bug: 265653158, where users reported slowness
    // of guest system (too much disk io) due to filebacked quick boot
    // enabled on btrfs and xfs etc
    virtual bool pathFileSystemIsExt4(fs::path path) const = 0;

    virtual std::optional<DiskKind> pathDiskKind(fs::path path) = 0;
    virtual std::optional<DiskKind> diskKind(int fd) = 0;

    // Scan directory |dirPath| for entries, and return them as a sorted
    // vector or entries. If |fullPath| is true, then each item of the
    // result vector contains a full path.
    virtual std::vector<fs::path> scanDirEntries(fs::path dirPath, bool fullPath = false) const = 0;

    /**
     * @brief Locates a bundled executable within the application's installation or development
     * environment.
     *
     * This function systematically searches for the specified executable (`programName`) in several
     * common locations, prioritizing the application's launcher directory before checking
     * its main program directory. It automatically appends the correct platform-specific
     * executable extension (e.g., `.exe` on Windows, no extension on Linux/macOS).
     *
     * The search order is as follows:
     * 1. The launcher directory:
     * - `[launcher_directory]/[programName]`
     * - `[launcher_directory]/bin/[programName]`
     * 2. The program's main installation directory:
     * - `[program_directory]/[programName]`
     * - `[program_directory]/bin/[programName]`
     *
     * If the executable isn't found in any of these standard locations and the application
     * is running within a Bazel development environment, the search extends to specific
     * Bazel-related paths to facilitate development workflows:
     * - `_main/third_party/qemu/[programName]` within the Bazel runfiles.
     * - `_main/hardware/generic/goldfish/third_party/sparse/[programName]` within the Bazel
     * runfiles.
     *
     * @param programName A `std::string_view` representing the base name of the executable
     * to find (e.g., "emulator", "adb"). The function handles appending
     * the platform-specific executable extension automatically.
     *
     * @return A `fs::path` object representing the absolute path to the found executable.
     * Returns an empty `fs::path` if the executable cannot be located in any of
     * the searched directories. You can check for an empty path using `.empty()`.
     */
    static fs::path findBundledExecutable(std::string_view programName);

    // Return the path of the current program's directory.
    virtual const fs::path getProgramDirectory() const = 0;

    // Return the path of the emulator launcher's directory.
    virtual const fs::path getLauncherDirectory() const = 0;

    // Return the path to user's home directory (as defined in the
    // underlying platform) or an empty string if it can't be found
    virtual const fs::path getHomeDirectory() const = 0;

    // Return the path to user's App Data directory (only applies
    // in Microsoft Windows) or an empty string if it can't be found
    virtual const fs::path getAppDataDirectory() const = 0;

    // Return the current directory path. Because this can change at
    // runtime, this returns a new std::string instance, not a const-reference
    // to a constant one held by the object. Return an empty string if there is
    // a problem with the system when getting the current directory.
    virtual fs::path getCurrentDirectory() const = 0;

    // Set the current directory path. Returns true if the directory was
    // successfully changed.
    virtual bool setCurrentDirectory(fs::path directory) = 0;

    // Return the path of a temporary directory appropriate for the system.
    virtual fs::path getTempDir() const = 0;

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
    static std::string getProgramDirectoryFromPlatform();
    static WallDuration getSystemTimeUs();

    /**
     * @brief Converts a Unix-style octal file mode to std::filesystem::perms.
     *
     * @param octalMode The octal file mode (e.g., 0755).
     * @return The corresponding std::filesystem::perms representation.
     */
    static fs::perms octalModeToPerms(int octalMode);

    // Windows driver file querying functions
    static bool queryFileVersionInfo(fs::path path, int* major, int* minor, int* build_1,
                                     int* build_2);

  protected:
    size_t mMemorySize = 0;

    static System* setForTesting(System* system);
    static System* hostSystem();

    // Internal implementation of scanDirEntries() that can be used by
    // mock implementation using a fake file system rooted into a temporary
    // directory or something like that. Always returns short paths.
    static std::vector<fs::path> scanDirInternal(fs::path dirPath);

    static bool readSomeBytes(fs::path path, char* array, int pos, int size);

    static bool pathExistsInternal(fs::path path);
    static bool pathIsFileInternal(fs::path path);
    static bool pathIsDirInternal(fs::path path);
    static bool pathIsLinkInternal(fs::path path);
    static bool pathCanReadInternal(fs::path path);
    static bool pathCanWriteInternal(fs::path path);
    static bool pathCanExecInternal(fs::path path);
    static bool pathIsQcow2Internal(fs::path path);
    static bool pathFileSystemIsExt4Internal(fs::path path);
    static bool pathIsExt4Internal(fs::path path);
    static int pathOpenInternal(const char* filename, int oflag, int pmode);
    static bool deleteFileInternal(fs::path path);
    static bool pathFileSizeInternal(fs::path path, FileSize* outFileSize);
    static bool fileSizeInternal(int fd, FileSize* outFileSize);
    static bool pathFreeSpaceInternal(fs::path path, FileSize* spaceInBytes);
    static FileSize recursiveSizeInternal(fs::path path);
    static std::optional<Duration> pathCreationTimeInternal(fs::path path);
    static std::optional<Duration> pathModificationTimeInternal(fs::path path);
    static std::optional<DiskKind> diskKindInternal(fs::path path);
    static std::optional<DiskKind> diskKindInternal(int fd);

  private:
    DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace base
}  // namespace android

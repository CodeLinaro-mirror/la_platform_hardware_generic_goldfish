// Copyright (C) 2022 The Android Open Source Project
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

#include <chrono>
#include <future>
#include <istream>
#include <memory>
#include <optional>
#include <streambuf>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace android ::base {

class Command;

using CommandArguments = std::vector<std::string>;
using Pid = int;
using ProcessExitCode = int;

/**
 * Represents a process running within the operating system.
 */
class Process {
  public:
    virtual ~Process() = default;

    /**
     * @return The process ID (PID) of the process, or -1 if invalid.
     */
    // NOLINTNEXTLINE
    Pid pid() const { return pid_; };

    /**
     * @return The name of the process executable. Note that this information
     *         might not be immediately available, especially shortly after
     *         the process has been started.
     */
    virtual std::string Exe() const = 0;

    /**
     * Retrieves the exit code of the process. This method will block until
     * the process has finished or is detached.
     *
     * @return The process exit code. This can return INT_MIN in case of
     *         failures retrieving the exit code.
     */
    ProcessExitCode ExitCode() const;

    /**
     * Forcibly terminates the process (similar to sending SIGKILL).
     *
     * @return True if the process was successfully terminated, false otherwise.
     */
    virtual bool Terminate() = 0;

    /**
     * Checks if the process is currently alive according to the operating
     * system.
     *
     * @return True if the process is alive, false otherwise.
     */
    virtual bool IsAlive() const = 0;

    /**
     * Waits for the process to complete, or until the specified timeout
     * duration has elapsed.
     *
     * @param timeout_duration The maximum duration to wait for process
     *        completion.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    // NOLINTNEXTLINE
    virtual std::future_status WaitFor(const std::chrono::milliseconds timeout_duration) const {
        return WaitForKernel(timeout_duration);
    }

    /**
     * Waits for the process to complete, or until the specified time point
     * has been reached.
     *
     * @tparam Clock The clock type used for the timeout.
     * @tparam Duration The duration type used for the timeout.
     * @param timeout_time The time point at which the wait should timeout.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    // NOLINTNEXTLINE
    template <class Clock, class Duration>
    std::future_status WaitUntil(
            const std::chrono::time_point<Clock, Duration>& timeout_time) const {
        return WaitFor(timeout_time - std::chrono::steady_clock::now());
    };

    bool operator==(const Process& rhs) const { return (pid_ == rhs.pid_); }
    bool operator!=(const Process& rhs) const { return !operator==(rhs); }

    /**
     * Retrieves a Process object representing the process with the given PID.
     *
     * @param pid The process ID (PID) to search for.
     * @return A unique pointer to a Process object representing the process,
     *         or nullptr if no such process exists.
     */
    static std::unique_ptr<Process> FromPid(Pid pid);

    /**
     * Retrieves a list of Process objects representing processes whose
     * executable name contains the specified name substring.
     *
     * Note: There might be a delay between the creation of a process and its
     * appearance in the process list. This delay can vary depending on the
     * operating system and system load.
     *
     * @param name The name substring to search for in process names.
     * @return A vector of unique pointers to Process objects representing the
     *         matching processes. If no matching processes are found, the
     *         vector will be empty.
     */
    static std::vector<std::unique_ptr<Process>> FromName(const std::string& name);

    /**
     * @return A unique pointer to a Process object representing the current
     *         process.
     */
    static std::unique_ptr<Process> Me();

  protected:
    /**
     * Retrieves the exit code of the process without blocking.
     *
     * @return An optional containing the process exit code if available,
     *         or std::nullopt if the process is still running or the exit
     *         code cannot be retrieved.
     */
    virtual std::optional<ProcessExitCode> GetExitCode() const = 0;

    /**
     * Waits for the process to complete using an operating system-level call,
     * without using any additional polling mechanisms.
     *
     * @param timeout_duration The maximum duration to wait for process
     *        completion.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    virtual std::future_status WaitForKernel(std::chrono::milliseconds timeout_duration) const = 0;

    Pid pid_{-1};
};

/**
 * Represents the output (stdout and stderr) of a process.
 */
class ProcessOutput {
  public:
    virtual ~ProcessOutput() = default;

    /**
     * Consumes the entire output stream and returns it as a string.
     *
     * @return The entire process output as a string.
     */
    virtual std::string AsString() = 0;

    /**
     * Provides access to the output stream, which can be used to read the
     * process output incrementally. This method may block until data is
     * available from the child process.
     *
     * @return A reference to the output stream.
     */
    virtual std::istream& AsStream() = 0;
};

/**
 * The ProcessOverseer class is responsible for monitoring a child process
 * and capturing its output (stdout and stderr).
 */
class ProcessOverseer {
  public:
    virtual ~ProcessOverseer() = default;

    /**
     * Starts monitoring the child process and capturing its output.
     *
     * The overseer should:
     * - Write captured output to the provided `out` and `err`
     *   std::basic_streambuf objects.
     * - Sync the std::basic_streambuf objects when the corresponding output streams
     *   are closed by the child process.
     * - Return from this method when it can no longer read or write from the
     *   child process's stdout and stderr.
     *
     * @param out The std::basic_streambuf object to write captured stdout output to.
     * @param err The std::basic_streambuf object to write captured stderr output to.
     */
    virtual void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) = 0;

    /**
     * Stops monitoring the child process and releases any resources held by
     * the overseer.
     *
     * After this method returns:
     * - No further writes should be made to the `out` and `err`
     *   std::basic_streambuf objects.
     * - All resources associated with the overseer should be released.
     * - Calling the `start` method again should result in an error or return
     *   immediately.
     */
    virtual void Stop() = 0;
};

/**
 * A ProcessOverseer implementation that does nothing. This can be used for
 * detached processes or in testing scenarios where process output monitoring
 * is not required.
 */
class NullOverseer : public ProcessOverseer {
  public:
    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {}
    void Stop() override {}
};

/**
 * Represents a running process that can be interacted with, such as reading
 * its output or terminating it.
 *
 * You typically obtain an ObservableProcess object by executing a Command.
 *
 * Example:
 * ```cpp
 * auto p = Command::create({"ls"}).execute();
 * if (p->exitCode() == 0) {
 *     auto list = p->out()->asString();
 * }
 * ```
 */
class ObservableProcess : public Process {
  public:
    explicit ObservableProcess(bool daemon = false, bool inherit = false)
            : daemon_(daemon), inherit_(inherit) {}

    // Kills the process..
    ~ObservableProcess() override;

    /**
     * @return A pointer to the ProcessOutput object representing the child
     *         process's standard output (stdout), or nullptr if the process
     *         was started in detached mode.
     */
    ProcessOutput* Out() { return std_out_.get(); };

    /**
     * @return A pointer to the ProcessOutput object representing the child
     *         process's standard error (stderr), or nullptr if the process
     *         was started in detached mode.
     */
    ProcessOutput* Err() { return std_err_.get(); };

    /**
     * Detaches the process overseer, stopping the monitoring of the child
     * process's output and preventing the process from being automatically
     * terminated when the ObservableProcess object goes out of scope.
     *
     * After calling this method:
     * - You will no longer be able to read the child process's stdout and
     *   stderr.
     * - The child process will continue running even after the
     *   ObservableProcess object is destroyed.
     */
    void Detach();

    /**
     * Waits for the process to complete.
     *
     * If an overseer is active (capturing output), this method will also wait
     * for the overseer to finish capturing all output before returning.
     *
     * @param timeout_duration The maximum duration to wait.
     * @return std::future_status::ready if the process and overseer have completed,
     *         std::future_status::timeout otherwise.
     */
    std::future_status WaitFor(std::chrono::milliseconds timeout_duration) const override;

  protected:
    /**
     * Subclasses should implement this method to handle the actual process
     * creation and launch.
     *
     * @param args The command line arguments to pass to the child process.
     * @param captureOutput Whether to capture the child process's output
     *        (stdout and stderr).
     * @return An optional containing the PID of the newly created process if
     *         successful, or std::nullopt if process creation failed.
     */
    virtual std::optional<Pid> CreateProcess(const CommandArguments& args, bool capture_output,
                                             bool replace) = 0;

    /**
     * Creates the ProcessOverseer object responsible for monitoring the child
     * process and capturing its output.
     *
     * @return A unique pointer to the created ProcessOverseer object.
     */
    virtual std::unique_ptr<ProcessOverseer> CreateOverseer() = 0;

    // True if no overseer is needed
    bool daemon_{false};

    // True if we want to inherit all the fds/handles.
    bool inherit_{false};

  private:
    void RunOverseer();

    std::unique_ptr<ProcessOverseer> overseer_;
    std::unique_ptr<std::thread> overseer_thread_;
    bool overseer_active_ ABSL_GUARDED_BY(overseer_mutex_){false};
    mutable absl::Mutex overseer_mutex_;

    std::unique_ptr<ProcessOutput> std_out_;
    std::unique_ptr<ProcessOutput> std_err_;

    friend Command;
};
}  // namespace android::base

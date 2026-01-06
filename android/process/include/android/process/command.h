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

#include <cstdio>
#include <functional>
#include <memory>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include "android/process/process.h"

namespace android {
namespace base {

/**
 * @brief A Command that you can execute and observe.
 */
class Command {
  public:
    /**
     * @brief Alias for a function that creates ObservableProcess instances.
     */
    using ProcessFactory =
            std::function<std::unique_ptr<ObservableProcess>(CommandArguments, bool, bool)>;

    /**
     * @brief Sets the standard output buffer.
     *
     * @param stdout_buffer The buffer to use for standard output.
     * @return A reference to this Command object for chaining.
     */
    Command& withStdoutBuffer(std::basic_streambuf<char>* stdout_buffer);

    /**
     * @brief Sets the standard error buffer.
     *
     * @param stderr_buffer The buffer to use for standard error.
     * @return A reference to this Command object for chaining.
     */
    Command& withStderrBuffer(std::basic_streambuf<char>* stderr_buffer);

    /**
     * @brief Adds a single argument to the list of arguments.
     *
     * @param arg The argument to add.
     * @return A reference to this Command object for chaining.
     */
    Command& arg(const std::string& arg);

    /**
     * @brief Adds a list of arguments to the existing arguments.
     *
     * @param args The arguments to add.
     * @return A reference to this Command object for chaining.
     */
    Command& args(const CommandArguments& args);

    /**
     * @brief Launches the command as a daemon.
     *
     * You will not be able to read stderr/stdout, and the process will not be
     * terminated when the created process goes out of scope.
     *
     * @return A reference to this Command object for chaining.
     */
    Command& asDeamon();

    /**
     * @brief Sets the command to inherit all file handles.
     *
     * @return A reference to this Command object for chaining.
     */
    Command& inherit();

    /**
     * @brief Sets the command to replace the current process.
     *
     * This behaves similarly to execv.
     *
     * @return A reference to this Command object for chaining.
     */
    Command& replace();

    /**
     * @brief Launches the process.
     *
     * @return A unique pointer to the ObservableProcess representing the
     *         launched process.
     */
    std::unique_ptr<ObservableProcess> execute();

    /**
     * @brief Creates a new Command object.
     *
     * @param programWithArgs The program to execute, along with its arguments.
     * @return A Command object representing the command to execute.
     */
    static Command create(CommandArguments programWithArgs);

    /**
     * @brief Sets a custom ProcessFactory for testing.
     *
     * You likely only want to use this for testing. Implement your own factory
     * that produces an implemented process. Make sure to set to nullptr when
     * you want to revert to the default.
     *
     * @param factory The custom ProcessFactory to use.
     */
    static void setTestProcessFactory(ProcessFactory factory);

  protected:
    Command() = default;

    /**
     * @brief Constructor with initial command arguments.
     *
     * @param args The initial command arguments.
     */
    Command(CommandArguments args) : mArgs(std::move(args)) {};

  private:
    static ProcessFactory sProcessFactory;
    static ProcessFactory sTestFactory;

    CommandArguments mArgs;
    bool mDeamon{false};
    bool mCaptureOutput{false};
    bool mInherit{false};
    bool mReplace{false};
    std::basic_streambuf<char>* mStdout{nullptr};
    std::basic_streambuf<char>* mStderr{nullptr};
};
}  // namespace base
}  // namespace android

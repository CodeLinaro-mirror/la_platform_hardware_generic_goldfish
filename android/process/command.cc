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
#include "android/process/command.h"

#include <cassert>
#include <climits>
#include <cstdio>
#include <future>
#include <iterator>
#include <streambuf>

#include "absl/log/log.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) printf("%s:%d %F| " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android {
namespace base {

ProcessExitCode Process::exitCode() const {
    auto status = wait_for(std::chrono::hours(24 * 365 * 10));
    return status == std::future_status::ready ? getExitCode().value_or(INT_MIN) : INT_MIN;
}

class ProcessOutputImpl : public ProcessOutput {
  public:
    explicit ProcessOutputImpl(std::basic_streambuf<char>* buffer)
            : mBuffer(buffer), mStream(buffer) {
        DVLOG(1) << "Created process output with: " << (buffer == nullptr ? "nothing" : "buffer");
    }

    std::string asString() override { return {std::istreambuf_iterator<char>{asStream()}, {}}; }

    std::istream& asStream() override {
        mStream.clear();
        return mStream;
    }

    std::basic_streambuf<char>* getBuf() { return mBuffer; }

  private:
    std::basic_streambuf<char>* mBuffer;
    std::istream mStream;
};

void ObservableProcess::runOverseer() {
    const absl::MutexLock lk(&mOverseerMutex);
    DVLOG(1) << "Starting overseer to retrieve stderr/stdout of " << exe();
    auto* out = reinterpret_cast<ProcessOutputImpl*>(mStdOut.get())->getBuf();
    auto* err = reinterpret_cast<ProcessOutputImpl*>(mStdErr.get())->getBuf();
    DVLOG(1) << "Using out:" << out << ", err:" << err;
    mOverseer->start(out, err);

    // Make sure we are really closed, and trigger any listeners.
    // (in case an overseer forgot)
    // Stop the overseer (likely a nop)
    mOverseer->stop();
    VLOG(1) << "Stopped overseer";
    mOverseerActive = false;
}

std::future_status ObservableProcess::wait_for(
        const std::chrono::milliseconds timeout_duration) const {
    const absl::MutexLock lk(&mOverseerMutex);
    if (!mOverseerActive) {
        return wait_for_kernel(timeout_duration);
    }

    // We have the lock when this lambda is called.
    auto inactive = [this]() ABSL_NO_THREAD_SAFETY_ANALYSIS { return !mOverseerActive; };
    if (!mOverseerMutex.AwaitWithTimeout(absl::Condition(&inactive),
                                         absl::FromChrono(timeout_duration))) {
        return std::future_status::timeout;
    }
    return std::future_status::ready;
}

void ObservableProcess::detach() {
    if (mOverseer) mOverseer->stop();
}

ObservableProcess::~ObservableProcess() {
    if (mOverseer) mOverseer->stop();
    if (mOverseerThread) mOverseerThread->join();
};

Command& Command::withStdoutBuffer(std::basic_streambuf<char>* stdout_buffer) {
    assert(mDeamon == false);
    mStdout = stdout_buffer;
    mCaptureOutput = true;
    return *this;
}

Command& Command::withStderrBuffer(std::basic_streambuf<char>* stderr_buffer) {
    assert(mDeamon == false);
    mStderr = stderr_buffer;
    mCaptureOutput = true;
    return *this;
}

// Adds a single argument to the list of arguments.
Command& Command::arg(const std::string& arg) {
    mArgs.push_back(arg);
    return *this;
}

// Adds a list of arguments to the existing arguments
Command& Command::args(const CommandArguments& args) {
    mArgs.insert(std::end(mArgs), std::begin(args), std::end(args));
    return *this;
}

Command& Command::asDeamon() {
    assert(mCaptureOutput == false);
    mDeamon = true;
    return *this;
}

Command& Command::replace() {
    mReplace = true;
    return *this;
}

Command& Command::inherit() {
    mInherit = true;
    return *this;
}

Command Command::create(std::vector<std::string> programWithArgs) {
    return Command(std::move(programWithArgs));
}

std::unique_ptr<ObservableProcess> Command::execute() {
    std::unique_ptr<ObservableProcess> proc;
    if (sTestFactory) {
        [[unlikely]] proc = sTestFactory(mArgs, mDeamon, mInherit);
    } else {
        proc = sProcessFactory(mArgs, mDeamon, mInherit);
    }

    // Connect I/O
    proc->mStdOut = std::make_unique<ProcessOutputImpl>(mStdout);
    proc->mStdErr = std::make_unique<ProcessOutputImpl>(mStderr);

    // Completion handlers.
    auto running = proc->createProcess(mArgs, mCaptureOutput, mReplace);

    if (!running) {
        proc->mOverseer = std::unique_ptr<NullOverseer>();
        return proc;
    }
    proc->mPid = running.value();
    if (!mCaptureOutput) {
        proc->mOverseer = std::unique_ptr<NullOverseer>();
    } else {
        auto* raw = proc.get();
        // TODO(jansene): Use condition_variable to assure that
        // overseer is really running after this call.
        const absl::MutexLock lk(&proc->mOverseerMutex);
        proc->mOverseerActive = true;
        proc->mOverseer = proc->createOverseer();
        proc->mOverseerThread = std::make_unique<std::thread>([raw]() { raw->runOverseer(); });
    }

    return proc;
}

void Command::setTestProcessFactory(ProcessFactory factory) {
    sTestFactory = factory;
}

Command::ProcessFactory Command::sTestFactory = nullptr;

}  // namespace base
}  // namespace android

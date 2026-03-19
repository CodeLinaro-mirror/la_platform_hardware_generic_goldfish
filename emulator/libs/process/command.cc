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

#include "goldfish/synchronized_stream_buf.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) printf("%s:%d %F| " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::base {

ProcessExitCode Process::ExitCode() const {
    auto status = WaitFor(std::chrono::hours(24 * 365 * 10));
    return status == std::future_status::ready ? GetExitCode().value_or(INT_MIN) : INT_MIN;
}

class ProcessOutputImpl : public ProcessOutput {
  public:
    explicit ProcessOutputImpl(std::basic_streambuf<char>* buffer)
            : buffer_(buffer), stream_(buffer ? &buffer_ : nullptr) {
        DVLOG(1) << "Created process output with: " << (buffer == nullptr ? "nothing" : "buffer");
    }

    std::string AsString() override { return {std::istreambuf_iterator<char>{AsStream()}, {}}; }

    std::istream& AsStream() override {
        stream_.clear();
        return stream_;
    }

    goldfish::SynchronizedStreamBuf<char>* Buffer() {
        return buffer_.IsValid() ? &buffer_ : nullptr;
    }

  private:
    goldfish::SynchronizedStreamBuf<char> buffer_;
    std::istream stream_;
};

void ObservableProcess::RunOverseer() {
    {
        const absl::MutexLock lk(overseer_mutex_);
        DVLOG(1) << "Starting overseer to retrieve stderr/stdout of PID " << pid();
        auto* out = reinterpret_cast<ProcessOutputImpl*>(std_out_.get())->Buffer();
        auto* err = reinterpret_cast<ProcessOutputImpl*>(std_err_.get())->Buffer();
        DVLOG(1) << "Using out:" << out << ", err:" << err;
        overseer_->Start(out, err);

        // Make sure we are really closed, and trigger any listeners.
        // (in case an overseer forgot)
        // Stop the overseer (likely a nop)
        overseer_->Stop();
        VLOG(1) << "Stopped overseer";
        overseer_active_ = false;
    }
}

std::future_status ObservableProcess::WaitFor(
        const std::chrono::milliseconds timeout_duration) const {
    const absl::MutexLock lk(overseer_mutex_);
    if (!overseer_active_) {
        return WaitForKernel(timeout_duration);
    }

    // We have the lock when this lambda is called.
    auto inactive = [this]() ABSL_NO_THREAD_SAFETY_ANALYSIS { return !overseer_active_; };
    if (!overseer_mutex_.AwaitWithTimeout(absl::Condition(&inactive),
                                          absl::FromChrono(timeout_duration))) {
        return std::future_status::timeout;
    }
    return std::future_status::ready;
}

void ObservableProcess::Detach() {
    if (overseer_) overseer_->Stop();
    daemon_ = true;
}

ObservableProcess::~ObservableProcess() {
    if (overseer_) overseer_->Stop();
    if (overseer_thread_) overseer_thread_->join();
};

Command& Command::RedirectStdoutToUnsafe(std::basic_streambuf<char>* stdout_buffer) {
    assert(daemon_ == false);
    std_out_ = stdout_buffer;
    capture_output_ = true;
    return *this;
}

Command& Command::RedirectStderrToUnsafe(std::basic_streambuf<char>* stderr_buffer) {
    assert(daemon_ == false);
    std_err_ = stderr_buffer;
    capture_output_ = true;
    return *this;
}

// Adds a single argument to the list of arguments.
Command& Command::Arg(const std::string& arg) {
    args_.push_back(arg);
    return *this;
}

// Adds a list of arguments to the existing arguments
Command& Command::Args(const CommandArguments& args) {
    args_.insert(std::end(args_), std::begin(args), std::end(args));
    return *this;
}

Command& Command::Asdaemon() {
    assert(capture_output_ == false);
    daemon_ = true;
    return *this;
}

Command& Command::Replace() {
    replace_ = true;
    return *this;
}

Command& Command::Inherit() {
    inherit_ = true;
    return *this;
}

Command Command::Create(std::vector<std::string> program_with_args) {
    return Command(std::move(program_with_args));
}

std::unique_ptr<ObservableProcess> Command::Execute() {
    std::unique_ptr<ObservableProcess> proc;
    if (s_test_factory) {
        [[unlikely]] proc = s_test_factory(args_, daemon_, inherit_);
    } else {
        proc = s_process_factory(args_, daemon_, inherit_);
    }

    // Connect I/O
    proc->std_out_ = std::make_unique<ProcessOutputImpl>(std_out_);
    proc->std_err_ = std::make_unique<ProcessOutputImpl>(std_err_);

    // Completion handlers.
    auto running = proc->CreateProcess(args_, capture_output_, replace_);

    if (!running) {
        proc->overseer_ = std::unique_ptr<NullOverseer>();
        return proc;
    }
    proc->pid_ = running.value();
    if (!capture_output_) {
        proc->overseer_ = std::unique_ptr<NullOverseer>();
    } else {
        auto* raw = proc.get();
        // TODO(jansene): Use condition_variable to assure that
        // overseer is really running after this call.
        const absl::MutexLock lk(proc->overseer_mutex_);
        proc->overseer_active_ = true;
        proc->overseer_ = proc->CreateOverseer();
        proc->overseer_thread_ = std::make_unique<std::thread>([raw]() { raw->RunOverseer(); });
    }

    return proc;
}

void Command::SetTestProcessFactory(ProcessFactory factory) {
    s_test_factory = std::move(factory);
}

Command::ProcessFactory Command::s_test_factory = nullptr;

}  // namespace android::base

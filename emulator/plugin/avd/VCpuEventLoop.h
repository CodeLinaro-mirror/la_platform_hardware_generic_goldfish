// Copyright (C) 2025 The Android Open Source Project
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

#include "emulator/plugin/avd/qemu_cpu_wrapper.h"
#include "goldfish/async/event_loop.h"

namespace goldfish::avd_info {

class VCpuEventLoop : public ::goldfish::async::EventLoop {
  public:
    static int cpus_count() { return aemu_cpus_count(); }

    class VCpuTimer : public Timer {
      public:
        VCpuTimer(int cpuIndex, Task task) : mCpuIndex(cpuIndex), mTask(std::move(task)) {
            mCallback.f = onTimer;
            mCallback.opaque = this;
        }

        ~VCpuTimer() = default;

        void cancel() override {
            // We can't actually cancel - but we shouldn't allow rescheduling.
        }

        void schedule(std::chrono::milliseconds new_delay,
                      std::chrono::milliseconds new_interval) override {
            if (new_delay != std::chrono::milliseconds::zero()) {
                LOG(ERROR) << "VCpuTimer does not support delayed scheduling";
                return;
            }
            if (new_interval != std::chrono::milliseconds::zero()) {
                LOG(ERROR) << "VCpuTimer does not support interval scheduling";
                return;
            }

            if (!aemu_cpus_run_async(mCpuIndex, &mCallback)) {
                LOG(ERROR) << "Failed to schedule on vcpu: " << mCpuIndex;
            }
        }

      private:
        static void onTimer(void* opaque) {
            auto* self = static_cast<VCpuTimer*>(opaque);
            self->mTask();
        }

        int mCpuIndex;
        Task mTask;

        cpus_callback mCallback{};
    };

    VCpuEventLoop(int cpuIndex) : mCpuIndex(cpuIndex) {}

    ~VCpuEventLoop() = default;
    VCpuEventLoop(VCpuEventLoop&& other) : mCpuIndex(other.mCpuIndex) {}

    int getCpuIndex() const { return mCpuIndex; }

    std::future<absl::Status> shutdown() override {
        std::promise<absl::Status> p;
        p.set_value(absl::OkStatus());
        return p.get_future();
    }
    bool isOnLoopThread() const override { return false; }
    absl::Status postImmediately(Task task) override { return absl::UnimplementedError(""); }
    absl::Status postDelayed(Task task, std::chrono::milliseconds delay) override {
        return absl::UnimplementedError("");
    }

    std::shared_ptr<Timer> createTimer(Task task) override {
        return std::make_shared<VCpuTimer>(mCpuIndex, std::move(task));
    }

  private:
    int mCpuIndex;
};

}  // namespace goldfish::avd_info
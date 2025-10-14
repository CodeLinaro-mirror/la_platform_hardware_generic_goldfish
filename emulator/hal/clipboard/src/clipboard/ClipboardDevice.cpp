// Copyright 2024 The Android Open Source Project
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
#include "android/clipboard/ClipboardDevice.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/synchronization/mutex.h"

#include "android/goldfish/config/avd.h"
#include "goldfish/devices/qemud.h"

using android::goldfish::Avd;

namespace goldfish::devices::clipboard {

namespace {

// A state of read or write operation, encapsulating the data size + buffer
// + the transfer position.
struct ReadWriteState {
    std::vector<char> buffer;
    uint32_t dataSize;
    uint32_t processedBytes;
    bool dataSizeTransferred;

    ReadWriteState() { reset(); }

    // Return the data buffer to transfer and its size.
    char* data() {
        return (dataSizeTransferred ? buffer.data() : reinterpret_cast<char*>(&dataSize)) +
               processedBytes;
    }
    int size() const {
        return (dataSizeTransferred ? static_cast<int>(buffer.size()) : sizeof(dataSize)) -
               processedBytes;
    }

    ClipboardData view() { return ClipboardData(buffer.data(), buffer.size()); }

    // Check if the state's buffer is finished (nothing left to transfer)
    bool isFinished() const { return dataSizeTransferred && processedBytes == dataSize; }

    // Reset the state so it's safe to start a new transfer.
    void reset() {
        dataSize = 0;
        processedBytes = 0;
        dataSizeTransferred = false;
        buffer.clear();
    }
};
}  // namespace

class ClipboardDevice : public IClipboardDevice {
  public:
    ClipboardDevice() { VLOG(1) << "Clipboard device has been created"; }

    ~ClipboardDevice() {}
    void onConnect() override { VLOG(1) << "Clipboard device has been connected"; }
    void onClose() override { VLOG(1) << "Clipboard device has been disconnected"; }
    void onReceive(std::string_view data) override {
        if (mGuestReadState.size() == 0 && !mGuestReadState.dataSizeTransferred) {
            mGuestReadState.dataSizeTransferred = true;
            mGuestReadState.processedBytes = 0;
            // If we're reading from the guest clipboard, make sure the
            // buffer on our side has enough space.
            mGuestReadState.buffer.resize(mGuestReadState.dataSize);
        }
        memcpy(mGuestReadState.data(), data.data(), data.size());
        mGuestReadState.processedBytes += data.size();

        if (mGuestReadState.isFinished()) {
            auto clipboardData = mGuestReadState.view();
            VLOG(1) << "Clipboard update from guest to (" << clipboardData.size() << "):" << data;
            {
                absl::MutexLock lock(&mClipboardDataLock);
                mClipboardData = clipboardData;
            }
            fireEvent(clipboardData);
            mGuestReadState.reset();
        }
    }

    bool isEnabled() const override { return mEnabled; }

    void enable(bool enable) override { mEnabled = enable; }

    void setContents(ClipboardData contents) override {
        if (!mEnabled) {
            return;
        }
        VLOG(1) << "Clipboard update from host to (" << contents.size() << "):" << contents;
        uint32_t size = contents.size();

        // Ensure little-endian representation
        char size_buf[sizeof(uint32_t)];
        absl::little_endian::Store32(size_buf, size);

        socket()->send(std::string(size_buf, sizeof(size_buf)));
        socket()->send(std::string(contents));
    }

    ClipboardData getContents() const override {
        absl::MutexLock lock(&mClipboardDataLock);
        return mClipboardData;
    };

  private:
    ReadWriteState mGuestReadState;
    std::string mClipboardData ABSL_GUARDED_BY(mClipboardDataLock);
    bool mEnabled{true};
    mutable absl::Mutex mClipboardDataLock;  // protects mClipboardData
};

void IClipboardDevice::registerDevice(IConnectorRegistry* registry, EventLoop* clientLoop,
                                      EventLoop* qemuLoop) {
    registry->registerHalDevice(std::string(IClipboardDevice::serviceName), clientLoop, qemuLoop,
                                [] { return std::make_shared<ClipboardDevice>(); });
}

}  // namespace goldfish::devices::clipboard
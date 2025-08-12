#pragma once

#include <memory>

#include "android/clipboard/ClipboardDevice.h"

namespace goldfish::devices::clipboard {

/**
 * A fake clipboard device for testing.
 */
class FakeClipboardDevice : public IClipboardDevice,
                            public std::enable_shared_from_this<FakeClipboardDevice> {
  public:
    // IClipboardDevice implementation
    bool isEnabled() const override { return mEnabled; }
    void enable(bool enable) override { mEnabled = enable; }
    void setContents(ClipboardData contents) override {
        mContents = std::string(contents);
        fireEvent(mContents);
    }
    ClipboardData getContents() const override { return mContents; }

    // IPlug implementation
    bool onReceive(const void* data, size_t size) override { return true; }
    SocketPtr onUnplug() override { return nullptr; }

  private:
    bool mEnabled = true;
    std::string mContents;
};

}  // namespace goldfish::devices::clipboard

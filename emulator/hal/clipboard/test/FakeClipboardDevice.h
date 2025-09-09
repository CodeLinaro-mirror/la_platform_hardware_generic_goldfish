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

    void onConnect() override { VLOG(1) << "FakeClipboardDevice has been connected"; }
    void onClose() override { VLOG(1) << "FakeClipboardDevice has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "FakeClipboardDevice received " << data;
    }

  private:
    bool mEnabled = true;
    std::string mContents;
};

}  // namespace goldfish::devices::clipboard

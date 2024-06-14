/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct QEMUFile;

namespace vsock {

struct GoldfishVirtioVsockDevice;
struct IChannel;

using StreamKey = uint64_t;
using ChannelPtr = std::shared_ptr<IChannel>;

struct StreamHandle {
    StreamHandle();
    StreamHandle(StreamHandle &&rhs);
    ~StreamHandle();
    StreamHandle& operator=(StreamHandle rhs);

    bool ok() const { return mKey != 0; }
    bool sendAsync(const void *data, size_t size) const;
    void close();

    static void swap(StreamHandle &lhs, StreamHandle &rhs) {
        using std::swap;
        swap(lhs.mDev, rhs.mDev);
        swap(lhs.mKey, rhs.mKey);
    }

    // This function returns the previous channel to avoid
    // calling its ~IChannel inside.
    ChannelPtr replaceChannelLocked(ChannelPtr newChannel) const;

    StreamHandle(const StreamHandle &) = delete;
    StreamHandle& operator=(const StreamHandle &) = delete;

private:
    friend GoldfishVirtioVsockDevice;

    StreamHandle(GoldfishVirtioVsockDevice *dev, StreamKey key);
    void release();

    GoldfishVirtioVsockDevice *mDev = nullptr;
    StreamKey mKey = 0;
};

struct IChannel {
    using TypeId = std::string;

    virtual ~IChannel() {}
    virtual StreamHandle onGuestClose() = 0;
    virtual void onGuestConnected() = 0;
    virtual bool onReceive(const void *data, size_t size) = 0;
    virtual TypeId getTypeId() const { return {}; }
    virtual int saveToSnapshot(QEMUFile *) const { return -1; };
};

}  // namespace vsock

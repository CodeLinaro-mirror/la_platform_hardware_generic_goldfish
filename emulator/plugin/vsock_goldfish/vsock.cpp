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

#include <cassert>
#include <deque>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"
#include "goldfish/vsock/snapshot.h"
#include "vsock_low_level.h"

#include "goldfish/archive/QEMUFileReader.h"
#include "goldfish/archive/QEMUFileWriter.h"

extern "C" {
#include "qemu/compiler.h"
#include "standard-headers/linux/virtio_vsock.h"
}

#define DEBUG_MSG(FMT, ...) \
    fprintf(stderr, "%s:%d: " FMT "\n", __func__, __LINE__, __VA_ARGS__)

#define ASSERT(C) ({ do { \
        if (!(C)) { \
            DEBUG_MSG("'%s' is not true", #C); \
            ::abort(); \
        } \
    } while (false); true; })

namespace {
using goldfish::archive::IReader;
using goldfish::archive::IWriter;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugOrSocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;
using goldfish::vsock::HostPortListener;

constexpr uint32_t kDynamicPortsStart = 1U << 31;

struct UniqueIdAllocator {
    uint32_t get() {
        auto i = mReturnedIds.end();
        if (i != mReturnedIds.begin()) {
            --i;
            const uint32_t id = *i;
            mReturnedIds.erase(i);
            return id;
        } else {
            return ++mLastId;
        }
    }

    void put(const uint32_t id) {
        if (id == mLastId) {
            --mLastId;

            while (true) {
                const auto i = mReturnedIds.begin();
                if (i != mReturnedIds.end() && *i == mLastId) {
                    --mLastId;
                    mReturnedIds.erase(i);
                } else {
                    break;
                }
            }
        } else {
            ASSERT(id < mLastId);
            mReturnedIds.insert(id);
        }
    }

    void reset() {
        mLastId = 0;
        mReturnedIds.clear();
    }

    void saveToSnapshot(IWriter &writer) const {
        writer << mLastId << mReturnedIds.size();
        for (const uint32_t id : mReturnedIds) {
            writer << id;
        }
    }

    int loadFromSnapshot(IReader &reader) {
        mLastId = getUnsigned(reader);
        mReturnedIds.clear();
        for (size_t n = getUnsigned(reader); n > 0; --n) {
            mReturnedIds.insert(getUnsigned(reader));
        }

        return 0;
    }

    uint32_t mLastId = 0;
    std::set<uint32_t, std::greater<uint32_t>> mReturnedIds;
};

struct SocketBuffer {
    void append(const void *data, size_t size) {
        if (mConsumed > 0) {
            mBuf.erase(mBuf.begin(), mBuf.begin() + mConsumed);
            mConsumed = 0;
        }

        const uint8_t *data8 = static_cast<const uint8_t *>(data);
        mBuf.insert(mBuf.end(), data8, data8 + size);
    }

    std::pair<const void*, size_t> peek() const {
        return {mBuf.data() + mConsumed, mBuf.size() - mConsumed};
    }

    void consume(size_t size) {
        mConsumed += size;
    }

    void saveToSnapshot(IWriter &writer) const {
        const auto x = peek();
        writer << x.second;
        writer.write(x.first, x.second);
    }

    int loadFromSnapshot(IReader &reader) {
        mConsumed = 0;
        const uint32_t size = getUnsigned(reader);
        mBuf.resize(size);
        return (reader.read(mBuf.data(), size) == size) ? 0 : 1;
    }

    std::vector<uint8_t> mBuf;
    size_t mConsumed = 0;
};

struct GoldfishVirtioVsockDevice;

struct VsockStream : public goldfish::devices::cable::ISocket {
    VsockStream(GoldfishVirtioVsockDevice &dev,
                const uint32_t guest, const uint32_t host)
        : vsockDev(dev), guestPort(guest), hostPort(host) {}

    GoldfishVirtioVsockDevice &vsockDev;
    PlugPtr plug;
    SocketBuffer hostToGuestBuf;
    const uint32_t guestPort;
    const uint32_t hostPort;
    uint32_t guestBufAlloc = 0;  // guest's buffer size
    uint32_t guestFwdCnt = 0;    // how much the guest received
    uint32_t hostSentCnt = 0;    // how much the host sent
    uint32_t hostFwdCnt = 0;     // how much the host received
    uint8_t sendOpMask = 0;      // bitmask of OPs to send
    bool isConnected = false;

    void sendAsync(const void *data, size_t size) override;
    PlugPtr unplugImpl() override;

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    void sendOp(enum virtio_vsock_op op) {
        ASSERT(op > VIRTIO_VSOCK_OP_INVALID);
        ASSERT(op <= VIRTIO_VSOCK_OP_CREDIT_REQUEST);
        sendOpMask |= (1U << op);
    }
};

struct VsockStreamKey {
    VsockStreamKey(uint32_t guest, uint32_t host)
        : guestPort(guest), hostPort(host) {}

    const uint32_t guestPort;
    const uint32_t hostPort;
};

struct PlugOrSocketVisitor {
    PlugOrSocketVisitor(VsockStream &s) : stream(s) {}

    bool operator()(PlugPtr plug) const {
        ASSERT(plug);
        stream.plug = std::move(plug);
        return true;
    }

    bool operator()(SocketPtr socket) const {
        socket.release();
        return false;
    }

    VsockStream &stream;
};

struct GoldfishVirtioVsockDevice {
    SocketPtr connect(const uint32_t guestPort, PlugPtr plug) {
        ASSERT(plug);
        DEBUG_MSG("this=%p, guestPort=%u plug=%p", this, guestPort, plug.get());

        const std::lock_guard<std::mutex> lock(mStateMutex);
        const uint32_t hostPort = mSrcPortAllocator.get() + kDynamicPortsStart;

        const auto [streamI, inserted] = mStreams.emplace(*this, guestPort, hostPort);
        ASSERT(inserted);

        VsockStream &stream = const_cast<VsockStream &>(*streamI);
        stream.plug = std::move(plug);
        stream.sendOp(VIRTIO_VSOCK_OP_REQUEST);

        (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
        return SocketPtr(&stream);
    }

    bool listen(const uint32_t hostPort, HostPortListener hostPortListener) {
        DEBUG_MSG("this=%p, hostPort=%u", this, hostPort);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        return mHostPortListeners.insert(
            {hostPort, std::move(hostPortListener)}).second;
    }

    void setParentStateSnapshotHandlers(void *parent,
                                        int(*save)(const void *, IWriter &),
                                        int(*load)(void *, IReader &)) {
        mParentStateArg = parent;
        mParentStateSave = save;
        mParentStateLoad = load;
    }

    static GoldfishVirtioVsockDevice& getInstance() {
        static GoldfishVirtioVsockDevice instance;
        return instance;
    }

    GoldfishVirtioVsockDevice() {
        DEBUG_MSG("this=%p", this);
    }

    ~GoldfishVirtioVsockDevice() {
        DEBUG_MSG("this=%p", this);
    }

    void sendAsyncImpl(VsockStream &stream, const void *const data,
                       const size_t size) {
        DEBUG_MSG("this=%p", this);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        if (stream.isConnected) {
            stream.hostToGuestBuf.append(data, size);
            (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
        }
    }

    void unplugFromDevice(VsockStream &stream) {
        const std::lock_guard<std::mutex> lock(mStateMutex);
        recycleStreamLocked(stream, false, VIRTIO_VSOCK_OP_SHUTDOWN);
        mStreams.erase(stream);
        (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
    }

    void recycleStreamLocked(VsockStream &stream, const bool callOnUnplug,
                             const enum virtio_vsock_op sendOp) {
        DEBUG_MSG("this=%p stream=%p callOnUnplug=%d, sendOp=%d",
                  this, &stream, callOnUnplug, sendOp);

        if (callOnUnplug) {
            ASSERT(stream.plug);
            stream.plug->onUnplug().release();
        }

        if (sendOp != VIRTIO_VSOCK_OP_INVALID) {
            queueOrphanPacketLocked(stream, sendOp);
        }

        const uint32_t hostPort = stream.hostPort;
        if (hostPort >= kDynamicPortsStart) {
            mSrcPortAllocator.put(hostPort - kDynamicPortsStart);
        }
    }

    bool processPacketOpRequestLocked(const struct virtio_vsock_hdr &hdr) {
        const uint32_t hostPort = hdr.dst_port;
        const auto portListenerI = mHostPortListeners.find(hostPort);
        if (portListenerI != mHostPortListeners.end()) {
            const auto [streamI, inserted] =
                mStreams.emplace(*this, hdr.src_port, hostPort);
            if (inserted) {
                VsockStream &stream = const_cast<VsockStream &>(*streamI);

                if (std::visit(PlugOrSocketVisitor(stream),
                               (portListenerI->second)(SocketPtr(&stream)))) {
                    stream.guestBufAlloc = hdr.buf_alloc;
                    stream.guestFwdCnt = hdr.fwd_cnt;
                    stream.isConnected = true;
                    stream.sendOp(VIRTIO_VSOCK_OP_RESPONSE);
                    stream.plug->onConnect();
                    return true;
                } else {
                    mStreams.erase(streamI);
                    return false;
                }
            } else {
                // The control enters here only if the vsock driver is
                // misbehaving which we have never observed. If we do
                // get here the virtio-spec does not say what to do here.
                // https://lore.kernel.org/all/CAOGAQepPkUDF8vmzRiG41xOGhFdDuq8-EHmdTrdVrK6E346G7A@mail.gmail.com/
                DEBUG_MSG("duplicate src_port in VIRTIO_VSOCK_OP_REQUEST. "
                          "hdr={src_port=%u, dst_port=%u}}",
                          hdr.src_port, hdr.dst_port);
                return true;
            }
        } else {
            DEBUG_MSG("no listener for dst_port=%u", hostPort);
            return false;
        }
    }

    struct virtio_vsock_hdr preparePacketHeaderLocked(const uint32_t srcPort,
                                                      const uint32_t dstPort,
                                                      const enum virtio_vsock_op op,
                                                      const uint32_t hostFwdCnt,
                                                      const uint32_t len) const {
        constexpr uint32_t kHostBufAllocSize = 64 * 1024;
        const uint32_t flags = 0;

        struct virtio_vsock_hdr hdr = {
            //.src_cid = ...,  see virtio_vsock_send_packet_host_to_guest
            //.dst_cid = ...,
            .src_port = srcPort,
            .dst_port = dstPort,
            .len = len,
            .type = VIRTIO_VSOCK_TYPE_STREAM,
            .op = static_cast<uint16_t>(op),
            .flags = flags,
            .buf_alloc = kHostBufAllocSize,
            .fwd_cnt = hostFwdCnt,
        };

        return hdr;
    }

    struct virtio_vsock_hdr preparePacketHeaderLocked(const VsockStream &stream,
                                                      const enum virtio_vsock_op op,
                                                      const uint32_t len) const {
        return preparePacketHeaderLocked(stream.hostPort, stream.guestPort, op,
                                         stream.hostFwdCnt, len);
    }

    void queueOrphanPacketLocked(const struct virtio_vsock_hdr &hdr) {
        mOrphanPackets.push_back(hdr);
    }

    void queueOrphanPacketLocked(const VsockStream &stream,
                                 const enum virtio_vsock_op op) {
        queueOrphanPacketLocked(preparePacketHeaderLocked(stream, op, 0));
    }

    void queueOrphanPacketLocked(const struct virtio_vsock_hdr &request,
                                 const enum virtio_vsock_op op) {
        queueOrphanPacketLocked(preparePacketHeaderLocked(request.dst_port,
                                                          request.src_port,
                                                          op, 0, 0));
    }

    void realize(void *const dev,
                 const GoldfishVirtIOVSockDevAPI *const devApi) {
        DEBUG_MSG("this=%p, dev=%p, devApi=%p", this, dev, devApi);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        mQemuDev = dev;
        mQemuDevApi = devApi;
    }

    void unrealize() {
        DEBUG_MSG("this=%p", this);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        // TODO
    }

    void setStatus(const uint8_t status) {
        ASSERT(!(status & VIRTIO_CONFIG_S_FAILED));

        if (status & VIRTIO_CONFIG_S_NEEDS_RESET) {
            DEBUG_MSG("this=%p, status=S_NEEDS_RESET", this);
            const std::lock_guard<std::mutex> lock(mStateMutex);

            for (const VsockStream &stream : mStreams) {
                ASSERT(stream.plug);
                stream.plug->onUnplug().release();
            }

            mHostEvents.clear();
            mOrphanPackets.clear();
            mSrcPortAllocator.reset();
        } else if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
            DEBUG_MSG("this=%p, status=S_DRIVER_OK", this);
        } else {
            DEBUG_MSG("this=%p, status=0x%02X", this, status);
        }
    }

    void onPacketReceive(const struct virtio_vsock_hdr &hdr,
                         const void *data) {
        const std::lock_guard<std::mutex> lock(mStateMutex);
        if (hdr.op == VIRTIO_VSOCK_OP_REQUEST) {
            if (!processPacketOpRequestLocked(hdr)) {
                queueOrphanPacketLocked(hdr, VIRTIO_VSOCK_OP_RST);
            }
        } else {
            const VsockStreamKey key(hdr.src_port, hdr.dst_port);
            const auto streamI = mStreams.find(key);

            if (streamI != mStreams.end()) {
                VsockStream &stream = const_cast<VsockStream &>(*streamI);
                stream.guestBufAlloc = hdr.buf_alloc;
                stream.guestFwdCnt = hdr.fwd_cnt;

                switch (hdr.op) {
                case VIRTIO_VSOCK_OP_RESPONSE:
                    stream.isConnected = true;
                    stream.plug->onConnect();
                    break;

                case VIRTIO_VSOCK_OP_RST:
                    recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_INVALID);
                    mStreams.erase(streamI);
                    break;

                case VIRTIO_VSOCK_OP_SHUTDOWN:
                    recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_SHUTDOWN);
                    mStreams.erase(streamI);
                    break;

                case VIRTIO_VSOCK_OP_RW:
                    if (stream.isConnected &&
                            ASSERT(stream.plug) &&
                            stream.plug->onReceive(data, hdr.len)) {
                        stream.hostFwdCnt += hdr.len;
                        stream.sendOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
                    } else {
                        recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_SHUTDOWN);
                        mStreams.erase(streamI);
                    }
                    break;

                case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
                    // we already updated guest counters (guestBufAlloc and guestFwdCnt)
                    break;

                case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
                    stream.sendOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
                    break;

                default:
                    DEBUG_MSG("unexpected op=%u", hdr.op);
                    recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_RST);
                    mStreams.erase(streamI);
                    break;
                }
            } else if (hdr.op != VIRTIO_VSOCK_OP_RST) {
                DEBUG_MSG("unexpected packet {src=%u, dst=%u, len=%u, op=%u}",
                          hdr.src_port, hdr.dst_port, hdr.len, hdr.op);
                queueOrphanPacketLocked(hdr, VIRTIO_VSOCK_OP_RST);
            }
        }
    }

    int onPacketsSend() {
        DEBUG_MSG("this=%p", this);

        bool needNotify = false;
        VirtIOVSockSendResult sendResult;

        const std::lock_guard<std::mutex> lock(mStateMutex);
        const auto sendPacketHostToGuest = mQemuDevApi->sendPacketHostToGuest;

        while (!mOrphanPackets.empty()) {
            sendResult = (*sendPacketHostToGuest)(mQemuDev, &mOrphanPackets.front(), nullptr);
            if (VirtIOVSockSendNeedNotify(sendResult)) {
                needNotify = true;
            }

            if (VirtIOVSockSendIsVqFull(sendResult)) {
                return needNotify;
            } else {
                mOrphanPackets.pop_front();
            }
        }

        for (const VsockStream &cStream : mStreams) {
            VsockStream &stream = const_cast<VsockStream &>(cStream);
            size_t guestAvailSize = stream.guestBufAlloc -
                (stream.hostSentCnt - stream.guestFwdCnt);
            unsigned sendOpMask = stream.sendOpMask |
                ((guestAvailSize == 0) ?
                    (1U << VIRTIO_VSOCK_OP_CREDIT_REQUEST) : 0);

            if (sendOpMask) {
                static const enum virtio_vsock_op ops[] = {
                    VIRTIO_VSOCK_OP_REQUEST,
                    VIRTIO_VSOCK_OP_RESPONSE,
                    VIRTIO_VSOCK_OP_CREDIT_UPDATE,
                    VIRTIO_VSOCK_OP_CREDIT_REQUEST,
                };

                for (const auto op : ops) {
                    if (sendOpMask & (1U << op)) {
                        auto hdr = preparePacketHeaderLocked(stream, op, 0);
                        sendResult = (*sendPacketHostToGuest)(mQemuDev, &hdr, nullptr);
                        if (VirtIOVSockSendNeedNotify(sendResult)) {
                            needNotify = true;
                        }
                        if (VirtIOVSockSendIsVqFull(sendResult)) {
                            stream.sendOpMask = sendOpMask;
                            return needNotify;
                        } else {
                            sendOpMask &= ~(1U << op);
                        }
                    }
                }

                stream.sendOpMask = 0;
            }

            while (guestAvailSize > 0) {
                const auto [data, chunkSize] = stream.hostToGuestBuf.peek();
                if (chunkSize == 0) {
                    break;
                }

                const size_t sendSize = std::min(chunkSize, guestAvailSize);
                auto hdr = preparePacketHeaderLocked(stream,
                                                     VIRTIO_VSOCK_OP_RW,
                                                     sendSize);
                sendResult = (*sendPacketHostToGuest)(mQemuDev, &hdr, data);
                stream.hostToGuestBuf.consume(sendSize);
                stream.hostSentCnt += sendSize;
                guestAvailSize -= sendSize;

                if (VirtIOVSockSendNeedNotify(sendResult)) {
                    needNotify = true;
                }
                if (VirtIOVSockSendIsVqFull(sendResult)) {
                    return needNotify;
                }
            }
        }

        return needNotify;
    }

    int onEventsSend() {
        DEBUG_MSG("this=%p", this);
        bool needNotify = false;

        const std::lock_guard<std::mutex> lock(mStateMutex);
        // TODO
        return needNotify;
    }

    int saveToSnapshot(IWriter &writer) const {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateSave) {
            r = (*mParentStateSave)(mParentStateArg, writer);
            if (r) {
                return r;
            }
        }

        const std::lock_guard<std::mutex> lock(mStateMutex);
        mSrcPortAllocator.saveToSnapshot(writer);

        writer << mOrphanPackets.size();
        for (const auto &packet : mOrphanPackets) {
            writer << packet.src_port << packet.dst_port
                   << packet.op << packet.flags
                   << packet.buf_alloc << packet.fwd_cnt;
        }

        writer << mStreams.size();
        for (const VsockStream &stream : mStreams) {
            writer << stream.guestPort << stream.hostPort << stream.hostFwdCnt;

            ASSERT(stream.plug);
            const IPlug &plug = *stream.plug;
            const bool supportsLoading = plug.supportsLoadingFromSnapshot();
            writer << supportsLoading;
            if (supportsLoading) {
                const unsigned flags = (stream.isConnected ? 1U : 0U) | stream.sendOpMask;

                writer << stream.guestBufAlloc << stream.guestFwdCnt
                       << stream.hostSentCnt << flags;

                stream.hostToGuestBuf.saveToSnapshot(writer);

                if (!savePlugToSnapshot(plug, writer)) {
                    return 1;
                }
            }
        }

        return 0;
    }

    int loadFromSnapshot(IReader &reader) {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateLoad) {
            r = (*mParentStateLoad)(mParentStateArg, reader);
            if (r) {
                return r;
            }
        }

        const std::lock_guard<std::mutex> lock(mStateMutex);
        r = mSrcPortAllocator.loadFromSnapshot(reader);
        if (r) {
            return r;
        }

        mOrphanPackets.clear();
        for (size_t n = getUnsigned(reader); n > 0; --n) {
            decltype(mOrphanPackets)::value_type packet;

            packet.src_port = getUnsigned(reader);
            packet.dst_port = getUnsigned(reader);
            packet.op = getUnsigned(reader);
            packet.flags = getUnsigned(reader);
            packet.buf_alloc = getUnsigned(reader);
            packet.fwd_cnt = getUnsigned(reader);
            packet.len = 0;  // orphan packets don't carry data
            packet.type = VIRTIO_VSOCK_TYPE_STREAM;
            mOrphanPackets.push_back(packet);
        }

        bool need_notify = false;
        mStreams.clear();
        for (size_t n = getUnsigned(reader); n > 0; --n) {
            const uint32_t guestPort = getUnsigned(reader);
            const uint32_t hostPort = getUnsigned(reader);
            const uint32_t hostFwdCnt = getUnsigned(reader);
            const bool supportsLoading = (getUnsigned(reader) != 0);
            if (supportsLoading) {
                const auto [streamI, inserted] =
                    mStreams.emplace(*this, guestPort, hostPort);
                if (!inserted) {
                    return 1;
                }

                VsockStream &stream = const_cast<VsockStream &>(*streamI);

                stream.hostFwdCnt = hostFwdCnt;
                stream.guestBufAlloc = getUnsigned(reader);
                stream.guestFwdCnt = getUnsigned(reader);
                stream.hostSentCnt = getUnsigned(reader);
                {
                    const uint8_t flags = getUnsigned(reader);
                    stream.isConnected = (flags & 1U) != 0;
                    stream.sendOpMask = flags & ~1U;
                }
                stream.hostToGuestBuf.loadFromSnapshot(reader);

                if (std::visit(PlugOrSocketVisitor(stream),
                               loadPlugFromSnapshot(SocketPtr(&stream), reader))) {
                    return true;
                } else {
                    mStreams.erase(streamI);
                    return 1;
                }
            } else {
                // this stream does not support loading from
                // a snapshot, send RST to the guest
                queueOrphanPacketLocked(preparePacketHeaderLocked(
                    hostPort, guestPort, VIRTIO_VSOCK_OP_RST, hostFwdCnt, 0));
                need_notify = true;
            }
        }

        if (need_notify) {
            (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
        }

        return 0;
    }

    static GoldfishVirtioVsockDevice &from(void *ptr) {
        return *static_cast<GoldfishVirtioVsockDevice *>(ptr);
    }

    static const GoldfishVirtioVsockDevice &from(const void *ptr) {
        return *static_cast<const GoldfishVirtioVsockDevice *>(ptr);
    }

    struct VsockStreamComparer {
        using is_transparent = void;

        bool operator()(const VsockStream &lhs,
                        const VsockStream &rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) <
                   std::tie(rhs.guestPort, rhs.hostPort);
        }

        bool operator()(const VsockStreamKey &lhs,
                        const VsockStream &rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) <
                   std::tie(rhs.guestPort, rhs.hostPort);
        }

        bool operator()(const VsockStream &lhs,
                        const VsockStreamKey &rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) <
                   std::tie(rhs.guestPort, rhs.hostPort);
        }
    };

    // IMPORTANT: std::set::erase
    // Other iterators and references are not invalidated.
    using Streams = std::set<VsockStream, VsockStreamComparer>;

    mutable std::mutex mStateMutex;

    void *mQemuDev = nullptr;
    const GoldfishVirtIOVSockDevAPI *mQemuDevApi = nullptr;
    void *mParentStateArg = nullptr;
    int (*mParentStateSave)(const void *, IWriter &) = nullptr;
    int (*mParentStateLoad)(void *, IReader &) = nullptr;
    std::unordered_map<uint32_t, HostPortListener> mHostPortListeners;

    // Everything below is snapshotted
    UniqueIdAllocator mSrcPortAllocator;
    std::deque<struct virtio_vsock_hdr> mOrphanPackets;
    std::deque<struct virtio_vsock_event> mHostEvents;
    Streams mStreams;
};

///////////////////////////////////////////////////////////////////////////////
void VsockStream::sendAsync(const void *data, size_t size) {
    DEBUG_MSG("this=%p vsockDev=%p size=%zu", this, &vsockDev, size);
    return vsockDev.sendAsyncImpl(*this, data, size);
}

PlugPtr VsockStream::unplugImpl() {
    DEBUG_MSG("this=%p vsockDev=%p", this, &vsockDev);
    PlugPtr p = std::move(plug);
    vsockDev.unplugFromDevice(*this);  // calls ~VsockStream
    return p;
}
}  // namespace

namespace goldfish {
namespace vsock {
using devices::cable::IPlug;
using devices::cable::SocketPtr;

SocketPtr connect(const uint32_t guestPort, PlugPtr plug) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.connect(guestPort, std::move(plug));
}

bool listen(const uint32_t hostPort, HostPortListener listener) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.listen(hostPort, std::move(listener));
}

void setParentStateSnapshotHandlers(void *parent,
                                    int(*save)(const void *, archive::IWriter &),
                                    int(*load)(void *, archive::IReader &)) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.setParentStateSnapshotHandlers(parent, save, load);
}
}  // namespace vsock
}  // namespace goldfish

///////////////////////////////////////////////////////////////////////////////////////
void* goldfish_virtio_vsock_impl_realize(void *dev,
                                         const GoldfishVirtIOVSockDevAPI *devApi) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    instance.realize(dev, devApi);
    return &instance;
}

void goldfish_virtio_vsock_impl_unrealize(void *impl) {
    GoldfishVirtioVsockDevice::from(impl).unrealize();
}

void goldfish_virtio_vsock_set_status(void *impl, uint8_t status) {
    GoldfishVirtioVsockDevice::from(impl).setStatus(status);
}

void goldfish_virtio_vsock_accept_guest_to_host(void *impl,
                                                const struct virtio_vsock_hdr* hdr,
                                                const void *data) {
    GoldfishVirtioVsockDevice::from(impl).onPacketReceive(*hdr, data);
}

int goldfish_virtio_vsock_handle_host_to_guest(void *impl) {
    return GoldfishVirtioVsockDevice::from(impl).onPacketsSend();
}

int goldfish_virtio_vsock_handle_event_to_guest(void *impl) {
    return GoldfishVirtioVsockDevice::from(impl).onEventsSend();
}

int goldfish_virtio_vsock_impl_save(const void *impl, QEMUFile *f) {
    goldfish::archive::QEMUFileWriter writer(f);
    return GoldfishVirtioVsockDevice::from(impl).saveToSnapshot(writer);
}

int goldfish_virtio_vsock_impl_load(void *impl, QEMUFile *f) {
    goldfish::archive::QEMUFileReader reader(f);
    return GoldfishVirtioVsockDevice::from(impl).loadFromSnapshot(reader);
}

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
#include <vector>

#include "goldfish/vsock/Channel.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"
#include "goldfish/vsock/snapshot.h"
#include "vsock_low_level.h"

#include "goldfish/QEMUFile.h"

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
constexpr uint32_t kDynamicPortsStart = 1U << 31;

uint64_t makeStreamKey(uint32_t guestPort, uint32_t hostPort) {
    return (uint64_t(guestPort) << 32) | hostPort;
}

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

    void saveToSnapshot(QEMUFile *file) const {
        qemu_put_be32(file, mLastId);
        qemu_put_be32(file, mReturnedIds.size());
        for (const uint32_t id : mReturnedIds) {
            qemu_put_be32(file, id);
        }
    }

    int loadFromSnapshot(QEMUFile *file) {
        mLastId = qemu_get_be32(file);
        mReturnedIds.clear();
        for (size_t n = qemu_get_be32(file); n > 0; --n) {
            mReturnedIds.insert(qemu_get_be32(file));
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

    void saveToSnapshot(QEMUFile *file) const {
        const auto x = peek();
        qemu_put_be32(file, x.second);
        qemu_put_buffer(file,
                        reinterpret_cast<const uint8_t *>(x.first),
                        x.second);
    }

    int loadFromSnapshot(QEMUFile *file) {
        mConsumed = 0;
        const uint32_t size = qemu_get_be32(file);
        mBuf.resize(size);
        return (qemu_get_buffer(file,
                                reinterpret_cast<uint8_t *>(mBuf.data()),
                                size) == size) ? 0 : 1;
    }

    std::vector<uint8_t> mBuf;
    size_t mConsumed = 0;
};
}  // namespace

namespace vsock {
struct GoldfishVirtioVsockDevice {
    struct Stream {
        ChannelPtr channel;
        SocketBuffer hostToGuestBuf;
        uint32_t guestPort = 0;
        uint32_t hostPort = 0;
        uint32_t guestBufAlloc = 0;  // guest's buffer size
        uint32_t guestFwdCnt = 0;    // how much the guest received
        uint32_t hostSentCnt = 0;    // how much the host sent
        uint32_t hostFwdCnt = 0;     // how much the host received
        uint8_t sendOpMask = 0;      // bitmask of OPs to send
        bool isConnected = false;

        void sendOp(enum virtio_vsock_op op) {
            ASSERT(op > VIRTIO_VSOCK_OP_INVALID);
            ASSERT(op <= VIRTIO_VSOCK_OP_CREDIT_REQUEST);
            sendOpMask |= (1U << op);
        }
    };

    StreamHandle connect(const uint32_t guestPort, ChannelPtr channel) {
        DEBUG_MSG("this=%p, guestPort=%u", this, guestPort);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        const uint32_t hostPort = mSrcPortAllocator.get() + kDynamicPortsStart;
        const StreamKey streamKey = makeStreamKey(guestPort, hostPort);

        const auto [streamI, inserted] = mStreams.try_emplace(streamKey);
        ASSERT(inserted);

        Stream &stream = streamI->second;

        stream.channel = std::move(channel);
        stream.guestPort = guestPort;
        stream.hostPort = hostPort;
        stream.sendOp(VIRTIO_VSOCK_OP_REQUEST);

        (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);

        return StreamHandle(this, streamKey);
    }

    bool listen(const uint32_t hostPort, HostPortListener hostPortListener) {
        DEBUG_MSG("this=%p, hostPort=%u", this, hostPort);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        return mHostPortListeners.insert(
            {hostPort, std::move(hostPortListener)}).second;
    }

    bool registerChannelLoader(IChannel::TypeId typeId, ChannelLoader loader) {
        DEBUG_MSG("this=%p, type=%s", this, typeId.c_str());

        const std::lock_guard<std::mutex> lock(mStateMutex);
        return mChannelLoaders.insert({std::move(typeId),
                                       std::move(loader)}).second;
    }

    void setParentStateSnapshotHandlers(void *parent,
                                        int(*save)(const void *, QEMUFile *),
                                        int(*load)(void *, QEMUFile *)) {
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

    bool sendAsyncImpl(const StreamKey key, const void *const data,
                       const size_t size) {
        DEBUG_MSG("this=%p", this);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        const auto streamI = mStreams.find(key);
        if (streamI != mStreams.end()) {
            Stream& stream = streamI->second;
            if (stream.isConnected) {
                stream.hostToGuestBuf.append(data, size);
                (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    ChannelPtr replaceChannelLocked(StreamKey key, ChannelPtr newChannel) {
        DEBUG_MSG("this=%p, key=%zu", this, size_t(key));

        const auto streamI = mStreams.find(key);
        if (streamI != mStreams.end()) {
            streamI->second.channel.swap(newChannel);
            return newChannel;
        } else {
            return {};
        }
    }

    void closeFromStreamHandle(const StreamKey key) {
        DEBUG_MSG("this=%p", this);

        const std::lock_guard<std::mutex> lock(mStateMutex);
        const auto streamI = mStreams.find(key);
        ASSERT(streamI != mStreams.end());

        recycleStreamLocked(streamI->second, false,
                            VIRTIO_VSOCK_OP_SHUTDOWN);
        mStreams.erase(streamI);

        (*mQemuDevApi->haveHostToGuestPackets)(mQemuDev);
    }

    void recycleStreamLocked(Stream &stream, const bool callOnClose,
                             const enum virtio_vsock_op sendOp) {
        DEBUG_MSG("this=%p, callOnClose=%d, sendOp=%d",
                  this, callOnClose, sendOp);

        if (stream.channel && callOnClose) {
            stream.channel->onGuestClose().release();
            stream.channel.reset();
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
        const auto portListenerI = mHostPortListeners.find(hdr.dst_port);
        if (portListenerI != mHostPortListeners.end()) {
            const StreamKey streamKey = makeStreamKey(hdr.src_port, hdr.dst_port);

            struct HandleNewListener {
                HandleNewListener(StreamsMap &streams0,
                                  const struct virtio_vsock_hdr &hdr0,
                                  const StreamKey streamKey0)
                    : streams(streams0), hdr(hdr0), streamKey(streamKey0) {}

                bool operator()(ChannelPtr channel) const {
                    const auto [streamI, inserted] = streams.try_emplace(streamKey);
                    Stream &stream = streamI->second;

                    if (inserted) {
                        stream.channel = std::move(channel);
                        ASSERT(stream.channel);

                        stream.guestPort = hdr.src_port;
                        stream.hostPort = hdr.dst_port;
                        stream.guestBufAlloc = hdr.buf_alloc;
                        stream.guestFwdCnt = hdr.fwd_cnt;

                        stream.isConnected = true;
                        stream.channel->onGuestConnected();

                        stream.sendOp(VIRTIO_VSOCK_OP_RESPONSE);
                    } else {
                        // The control enters here only if the vsock driver is
                        // misbehaving which we have never observed. If we do
                        // get here the virtio-spec does not say what to do here.
                        // https://lore.kernel.org/all/CAOGAQepPkUDF8vmzRiG41xOGhFdDuq8-EHmdTrdVrK6E346G7A@mail.gmail.com/
                        DEBUG_MSG("duplicate src_port in VIRTIO_VSOCK_OP_REQUEST. "
                                  "hdr={src_port=%u, dst_port=%u} stream={hostPort=%u, guestPort=%u}",
                                  hdr.src_port, hdr.dst_port, stream.hostPort, stream.guestPort);

                        channel->onGuestClose().release();
                    }

                    return true;
                }

                bool operator()(StreamHandle handle) const {
                    // we have not inserted this key into mStreams yet
                    handle.release();
                    return false;
                }

                StreamsMap &streams;
                const struct virtio_vsock_hdr &hdr;
                const StreamKey streamKey;
            };

            return std::visit(HandleNewListener(mStreams, hdr, streamKey),
                              (portListenerI->second)(StreamHandle(this, streamKey)));
        } else {
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

    struct virtio_vsock_hdr preparePacketHeaderLocked(const Stream &stream,
                                                      const enum virtio_vsock_op op,
                                                      const uint32_t len) const {
        return preparePacketHeaderLocked(stream.hostPort, stream.guestPort, op,
                                         stream.hostFwdCnt, len);
    }

    void queueOrphanPacketLocked(const Stream &stream,
                                 const enum virtio_vsock_op op) {
        mOrphanPackets.push_back(preparePacketHeaderLocked(stream, op, 0));
    }

    void queueOrphanPacketLocked(const struct virtio_vsock_hdr& request,
                                 const enum virtio_vsock_op op) {
        mOrphanPackets.push_back(preparePacketHeaderLocked(request.dst_port,
                                                           request.src_port,
                                                           op, 0, 0));
    }

    ChannelPtr loadChannelByTypeIdLocked(const IChannel::TypeId &typeId,
                                         QEMUFile *const file) const {
        const auto i = mChannelLoaders.find(typeId);
        if (i != mChannelLoaders.end()) {
            return (i->second)(file);
        } else {
            return {};
        }
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

            for (auto &kv : mStreams) {
                const auto &channel = kv.second.channel;
                if (channel) {
                    channel->onGuestClose().release();
                }
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
                DEBUG_MSG("no listener for dst_port=%u", hdr.dst_port);
                queueOrphanPacketLocked(hdr, VIRTIO_VSOCK_OP_RST);
            }
        } else {
            const auto streamI = mStreams.find(makeStreamKey(hdr.src_port,
                                                             hdr.dst_port));

            if (streamI != mStreams.end()) {
                Stream &stream = streamI->second;
                stream.guestBufAlloc = hdr.buf_alloc;
                stream.guestFwdCnt = hdr.fwd_cnt;

                switch (hdr.op) {
                case VIRTIO_VSOCK_OP_RESPONSE:
                    stream.isConnected = true;
                    stream.channel->onGuestConnected();
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
                            ASSERT(stream.channel) &&
                            stream.channel->onReceive(data, hdr.len)) {
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

        for (auto &kv : mStreams) {
            Stream &stream = kv.second;
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

    int saveToSnapshot(QEMUFile *const file) const {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateSave) {
            r = (*mParentStateSave)(mParentStateArg, file);
            if (r) {
                return r;
            }
        }

        const std::lock_guard<std::mutex> lock(mStateMutex);
        mSrcPortAllocator.saveToSnapshot(file);

        qemu_put_be32(file, mOrphanPackets.size());
        for (const auto &packet : mStreams) {
            qemu_put_buffer(file, reinterpret_cast<const uint8_t *>(&packet),
                                  sizeof(packet));
        }

        qemu_put_be32(file, mStreams.size());
        for (const auto &kv : mStreams) {
            const auto& stream = kv.second;

            qemu_put_be32(file, stream.guestPort);
            qemu_put_be32(file, stream.hostPort);
            qemu_put_be32(file, stream.hostFwdCnt);

            const IChannel &channel = *stream.channel;
            const std::string channelTypeId = channel.getTypeId();
            const size_t channelTypeIdSize = channelTypeId.size();
            if (channelTypeIdSize > UINT8_MAX) {
                return 1;
            }

            qemu_put_byte(file, channelTypeIdSize);
            if (channelTypeIdSize > 0) {
                qemu_put_buffer(file,
                                reinterpret_cast<const uint8_t *>(channelTypeId.data()),
                                channelTypeIdSize);

                qemu_put_be32(file, stream.guestBufAlloc);
                qemu_put_be32(file, stream.guestFwdCnt);
                qemu_put_be32(file, stream.hostSentCnt);
                qemu_put_byte(file, (stream.isConnected ? 1U : 0U) | stream.sendOpMask);
                stream.hostToGuestBuf.saveToSnapshot(file);
                r = channel.saveToSnapshot(file);
                if (r) {
                    return r;
                }
            }
        }

        return 0;
    }

    int loadFromSnapshot(QEMUFile *const file) {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateLoad) {
            r = (*mParentStateLoad)(mParentStateArg, file);
            if (r) {
                return r;
            }
        }

        const std::lock_guard<std::mutex> lock(mStateMutex);
        r = mSrcPortAllocator.loadFromSnapshot(file);
        if (r) {
            return r;
        }

        mOrphanPackets.clear();
        for (uint32_t n = qemu_get_be32(file); n > 0; --n) {
            decltype(mOrphanPackets)::value_type packet;
            if (qemu_get_buffer(file,
                                reinterpret_cast<uint8_t *>(&packet),
                                sizeof(packet)) != sizeof(packet)) {
                return 1;
            }
            mOrphanPackets.push_back(packet);
        }

        bool need_notify = false;
        mStreams.clear();
        for (uint32_t n = qemu_get_be32(file); n > 0; --n) {
            Stream stream;

            stream.guestPort = qemu_get_be32(file);
            stream.hostPort = qemu_get_be32(file);
            stream.hostFwdCnt = qemu_get_be32(file);

            const size_t channelTypeIdSize = qemu_get_byte(file);
            if (channelTypeIdSize) {
                std::string channelTypeId(channelTypeIdSize, '?');
                if (qemu_get_buffer(file,
                        reinterpret_cast<uint8_t *>(channelTypeId.data()),
                        channelTypeIdSize) != channelTypeIdSize) {
                    return 1;
                }

                stream.guestBufAlloc = qemu_get_be32(file);
                stream.guestFwdCnt = qemu_get_be32(file);
                stream.hostSentCnt = qemu_get_be32(file);
                {
                    const uint8_t flags = qemu_get_byte(file);
                    stream.isConnected = (flags & 1U) != 0;
                    stream.sendOpMask = flags & ~1U;
                }
                stream.hostToGuestBuf.loadFromSnapshot(file);
                stream.channel = loadChannelByTypeIdLocked(channelTypeId, file);
                if (stream.channel) {
                    const StreamKey streamKey = makeStreamKey(stream.guestPort,
                                                              stream.hostPort);
                    if (!mStreams.insert({streamKey, std::move(stream)}).second) {
                        return 1;
                    }
                }
            } else {
                // this stream does not support loading from
                // a snapshot, send RST to the guest
                queueOrphanPacketLocked(stream, VIRTIO_VSOCK_OP_RST);
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

    using StreamsMap = std::unordered_map<StreamKey, Stream>;

    mutable std::mutex mStateMutex;

    void *mQemuDev = nullptr;
    const GoldfishVirtIOVSockDevAPI *mQemuDevApi = nullptr;
    void *mParentStateArg = nullptr;
    int (*mParentStateSave)(const void *, QEMUFile *) = nullptr;
    int (*mParentStateLoad)(void *, QEMUFile *) = nullptr;
    std::unordered_map<uint32_t, HostPortListener> mHostPortListeners;
    std::unordered_map<IChannel::TypeId, ChannelLoader> mChannelLoaders;

    // Everything below is snapshotted
    UniqueIdAllocator mSrcPortAllocator;
    std::deque<struct virtio_vsock_hdr> mOrphanPackets;
    std::deque<struct virtio_vsock_event> mHostEvents;
    StreamsMap mStreams;
};

///////////////////////////////////////////////////////////////////////////////
StreamHandle::StreamHandle() {
    DEBUG_MSG("this=%p", this);
}

StreamHandle::StreamHandle(GoldfishVirtioVsockDevice *dev,
                           const StreamKey key)
        : mDev(dev), mKey(key) {
    DEBUG_MSG("this=%p dev=%p key=%zu", this, dev, size_t(key));
}

StreamHandle::StreamHandle(StreamHandle &&rhs)
        : mDev(std::exchange(rhs.mDev, nullptr))
        , mKey(std::exchange(rhs.mKey, 0)) {
    DEBUG_MSG("this=%p dev=%p key=%zu", this, mDev, size_t(mKey));
}

StreamHandle& StreamHandle::operator=(StreamHandle rhs) {
    swap(*this, rhs);
    DEBUG_MSG("this=%p dev=%p key=%zu", this, mDev, size_t(mKey));
    return *this;
}

StreamHandle::~StreamHandle() {
    DEBUG_MSG("this=%p dev=%p key=%zu", this, mDev, size_t(mKey));
    close();
}

bool StreamHandle::sendAsync(const void *data, size_t size) const {
    DEBUG_MSG("this=%p dev=%p key=%zu size=%zu",
              this, mDev, size_t(mKey), size);

    return mKey && mDev->sendAsyncImpl(mKey, data, size);
}

ChannelPtr StreamHandle::replaceChannelLocked(ChannelPtr newChannel) const {
    if (mKey) {
        return mDev->replaceChannelLocked(mKey, std::move(newChannel));
    } else {
        return {};
    }
}

void StreamHandle::close() {
    if (mKey) {
        DEBUG_MSG("this=%p dev=%p key=%zu", this, mDev, size_t(mKey));
        mDev->closeFromStreamHandle(mKey);
        mDev = nullptr;
        mKey = 0;
    }
}

void StreamHandle::release() {
    ASSERT(mKey != 0);
    DEBUG_MSG("this=%p dev=%p key=%zu", this, mDev, size_t(mKey));
    mDev = nullptr;
    mKey = 0;
}

///////////////////////////////////////////////////////////////////////////////
StreamHandle connect(const uint32_t guestPort, ChannelPtr channel) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.connect(guestPort, std::move(channel));
}

bool listen(const uint32_t hostPort, HostPortListener listener) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.listen(hostPort, std::move(listener));
}

bool registerChannelLoader(IChannel::TypeId typeId, ChannelLoader loader) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.registerChannelLoader(std::move(typeId),
                                          std::move(loader));
}

void setParentStateSnapshotHandlers(void *parent,
                                    int(*save)(const void *, QEMUFile *),
                                    int(*load)(void *, QEMUFile *)) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.setParentStateSnapshotHandlers(parent, save, load);
}

}  // namespace vsock

///////////////////////////////////////////////////////////////////////////////////////
void* goldfish_virtio_vsock_impl_realize(void *dev,
                                         const GoldfishVirtIOVSockDevAPI *devApi) {
    auto& instance = vsock::GoldfishVirtioVsockDevice::getInstance();
    instance.realize(dev, devApi);
    return &instance;
}

void goldfish_virtio_vsock_impl_unrealize(void *impl) {
    vsock::GoldfishVirtioVsockDevice::from(impl).unrealize();
}

void goldfish_virtio_vsock_set_status(void *impl, uint8_t status) {
    vsock::GoldfishVirtioVsockDevice::from(impl).setStatus(status);
}

void goldfish_virtio_vsock_accept_guest_to_host(void *impl,
                                                const struct virtio_vsock_hdr* hdr,
                                                const void *data) {
    vsock::GoldfishVirtioVsockDevice::from(impl).onPacketReceive(*hdr, data);
}

int goldfish_virtio_vsock_handle_host_to_guest(void *impl) {
    return vsock::GoldfishVirtioVsockDevice::from(impl).onPacketsSend();
}

int goldfish_virtio_vsock_handle_event_to_guest(void *impl) {
    return vsock::GoldfishVirtioVsockDevice::from(impl).onEventsSend();
}

int goldfish_virtio_vsock_impl_save(const void *impl, QEMUFile *f) {
    return vsock::GoldfishVirtioVsockDevice::from(impl).saveToSnapshot(f);
}

int goldfish_virtio_vsock_impl_load(void *impl, QEMUFile *f) {
    return vsock::GoldfishVirtioVsockDevice::from(impl).loadFromSnapshot(f);
}

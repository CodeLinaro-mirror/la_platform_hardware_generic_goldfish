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

#include <deque>
#include <set>
#include <unordered_map>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/archive/collections/deque.h"
#include "goldfish/archive/qemu_file_reader.h"
#include "goldfish/archive/qemu_file_writer.h"
#include "goldfish/debug.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/socket_buffer.h"
#include "goldfish/synchronization/mutex.h"
#include "goldfish/unique_id_allocator.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"
#include "goldfish/vsock/snapshot.h"
#include "goldfish/vsock/vsock_low_level.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/compiler.h"
#include "standard-headers/linux/virtio_vsock.h"
}
// IWYU pragma: end_keep
// clang-format on
#define DEBUG_MSG(FMT, ...)  // fprintf(stderr, "%s:%d: " FMT "\n", __func__, __LINE__, __VA_ARGS__)

goldfish::archive::IWriter& operator<<(goldfish::archive::IWriter& w,
                                       const struct virtio_vsock_hdr& x) {
    return w << x.src_cid << x.dst_cid << x.src_port << x.dst_port << x.len << x.type << x.op
             << x.flags << x.buf_alloc << x.fwd_cnt;
}

absl::Status ReadValue(goldfish::archive::IReader& r, struct virtio_vsock_hdr& x) {
    return ReadValue(r, x.src_cid, x.dst_cid, x.src_port, x.dst_port, x.len, x.type, x.op, x.flags,
                     x.buf_alloc, x.fwd_cnt);
}

namespace {
using goldfish::archive::IReader;
using goldfish::archive::IWriter;
using goldfish::devices::cable::IDataSniffer;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugOrSocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;
using goldfish::synchronization::MutexUnlock;
using goldfish::vsock::HostPortListener;

constexpr uint32_t kDynamicPortsStart = 1U << 31;
constexpr uint32_t kMaxReturnedHostPortsSize = 1024;
constexpr uint32_t kTicksPerHostPort = 1024;

struct GoldfishVirtioVsockDevice;

struct VsockStream : public goldfish::devices::cable::ISocket {
    VsockStream(GoldfishVirtioVsockDevice& dev, const uint32_t guest, const uint32_t host)
            : vsockDev(dev), guestPort(guest), hostPort(host) {}

    static constexpr size_t kBufferSizeHighWatermark = size_t(8) << 20;  // 8 MiB
    static constexpr size_t kBufferSizeLowWatermark = kBufferSizeHighWatermark / 2;

    GoldfishVirtioVsockDevice& vsockDev;
    PlugPtr plug;
    std::unique_ptr<IDataSniffer> dataSniffer;
    goldfish::SocketBuffer hostToGuestBuf;
    OnFlowControlEvent onFlowControlEvent;
    const uint32_t guestPort;
    const uint32_t hostPort;
    uint32_t guestBufAlloc = 0;  // guest's buffer size
    uint32_t guestFwdCnt = 0;    // how much the guest received
    uint32_t hostSentCnt = 0;    // how much the host sent
    uint32_t hostFwdCnt = 0;     // how much the host received
    uint8_t sendOpMask = 0;      // bitmask of OPs to send
    bool isConnected = false;
    bool producerEnabled = true;

    void SetOnFlowControlEvent(OnFlowControlEvent fce) override;

    void SendAsync(const void* data, size_t size) override;

    PlugPtr UnplugImpl() override;

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    void SetDataSniffer(std::unique_ptr<IDataSniffer> sniffer) override {
        dataSniffer = std::move(sniffer);
    }

    void sendOp(enum virtio_vsock_op op) {
        DCHECK(op > VIRTIO_VSOCK_OP_INVALID);
        DCHECK(op <= VIRTIO_VSOCK_OP_CREDIT_REQUEST);
        sendOpMask |= (1U << op);
    }

    void setProducerEnabled(const bool newValue) {
        // We don't want to spam the producer with the same value.
        if ((producerEnabled != newValue) && onFlowControlEvent) {
            producerEnabled = newValue;
            onFlowControlEvent(newValue);
        }
    }

    void AbslStringifyImpl(absl::FormatSink& sink) const override {
        absl::Format(&sink,
                     "[VsockStream %u <-> %u, %s,  gFwd:%u, "
                     "hSent:%u, hFwd:%u]",
                     hostPort, guestPort, isConnected ? "open" : "closed", guestFwdCnt, hostSentCnt,
                     hostFwdCnt);
    }
};

struct VsockStreamKey {
    VsockStreamKey(uint32_t guest, uint32_t host) : guestPort(guest), hostPort(host) {}

    const uint32_t guestPort;
    const uint32_t hostPort;
};

struct PlugOrSocketVisitor {
    PlugOrSocketVisitor(VsockStream& s) : stream(s) {}

    bool operator()(PlugPtr plug) const {
        stream.plug = std::move(NOT_NULL(plug));
        return true;
    }

    bool operator()(SocketPtr socket) const {
        socket.release();
        return false;
    }

    VsockStream& stream;
};

struct GoldfishVirtioVsockDevice {
    SocketPtr Connect(const uint32_t guestPort, PlugPtr plug) {
        DEBUG_MSG("this=%p, guestPort=%u plug=%p", this, guestPort, plug.get());

        const absl::MutexLock lock(mStateMutex);
        const uint32_t hostPort = mSrcPortAllocator.Get() + kDynamicPortsStart;

        const auto [streamI, inserted] = mStreams.emplace(*this, guestPort, hostPort);
        DCHECK(inserted);

        VsockStream& stream = const_cast<VsockStream&>(*streamI);
        stream.plug = std::move(NOT_NULL(plug));
        stream.sendOp(VIRTIO_VSOCK_OP_REQUEST);

        sendPacketsAndNotifyLocked();
        return SocketPtr(&stream);
    }

    void RecycleOneHostPort() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        DCHECK(!mReturnedHostPorts.empty());
        const uint32_t returnedPort = mReturnedHostPorts.front();
        mReturnedHostPorts.pop_front();
        DCHECK(returnedPort >= kDynamicPortsStart);
        mSrcPortAllocator.Put(returnedPort - kDynamicPortsStart);
    }

    bool Listen(const uint32_t hostPort, HostPortListener hostPortListener) {
        DEBUG_MSG("this=%p, hostPort=%u", this, hostPort);

        const absl::MutexLock lock(mStateMutex);
        return mHostPortListeners.insert({hostPort, std::move(hostPortListener)}).second;
    }

    void setParentStateSnapshotHandlers(void* parent, int (*save)(const void*, IWriter&),
                                        int (*load)(void*, IReader&)) {
        mParentStateArg = parent;
        mParentStateSave = save;
        mParentStateLoad = load;
    }

    static GoldfishVirtioVsockDevice& getInstance() {
        static GoldfishVirtioVsockDevice instance;
        return instance;
    }

    GoldfishVirtioVsockDevice() { DEBUG_MSG("this=%p", this); }

    ~GoldfishVirtioVsockDevice() { DEBUG_MSG("this=%p", this); }

    void sendAsyncImpl(VsockStream& stream, const void* const data, const size_t size) {
        DEBUG_MSG("this=%p", this);

        const absl::MutexLock lock(mStateMutex);
        if (stream.isConnected) {
            if (stream.dataSniffer) {
                stream.dataSniffer->ToSocket(data, size);
            }

            if (stream.hostToGuestBuf.Append(data, size) >= stream.kBufferSizeHighWatermark) {
                stream.setProducerEnabled(false);
            }

            sendPacketsAndNotifyLocked();
        }
    }

    void unplugFromDevice(VsockStream& stream) {
        const absl::MutexLock lock(mStateMutex);
        recycleStreamLocked(stream, false, VIRTIO_VSOCK_OP_SHUTDOWN);
        mStreams.erase(stream);
        sendPacketsAndNotifyLocked();
    }

    void recycleStreamLocked(VsockStream& stream, const bool callOnUnplug,
                             const enum virtio_vsock_op sendOp)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        DEBUG_MSG("this=%p stream=%p callOnUnplug=%d, sendOp=%d", this, &stream, callOnUnplug,
                  sendOp);

        if (callOnUnplug) {
            const MutexUnlock unlock(mStateMutex);
            NOT_NULL(stream.plug)->OnUnplug().release();
        }

        if (sendOp != VIRTIO_VSOCK_OP_INVALID) {
            queueOrphanPacketLocked(stream, sendOp);
        }

        const uint32_t hostPort = stream.hostPort;
        if (hostPort >= kDynamicPortsStart) {
            if (mReturnedHostPorts.size() >= kMaxReturnedHostPortsSize) {
                RecycleOneHostPort();
                DCHECK(mReturnedHostPorts.size() < kMaxReturnedHostPortsSize);
            }

            mReturnedHostPorts.push_back(hostPort);
        }
    }

    void onTick() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        if (++mTickCounter >= kTicksPerHostPort) {
            mTickCounter = 0;
            if (!mReturnedHostPorts.empty()) {
                RecycleOneHostPort();
            }
        }
    }

    bool processPacketOpRequestLocked(const struct virtio_vsock_hdr& hdr)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        const uint32_t hostPort = hdr.dst_port;
        const auto portListenerI = mHostPortListeners.find(hostPort);
        if (portListenerI != mHostPortListeners.end()) {
            const auto [streamI, inserted] = mStreams.emplace(*this, hdr.src_port, hostPort);
            if (inserted) {
                VsockStream& stream = const_cast<VsockStream&>(*streamI);

                if (std::visit(PlugOrSocketVisitor(stream),
                               (portListenerI->second)(SocketPtr(&stream)))) {
                    stream.guestBufAlloc = hdr.buf_alloc;
                    stream.guestFwdCnt = hdr.fwd_cnt;
                    stream.isConnected = true;
                    stream.sendOp(VIRTIO_VSOCK_OP_RESPONSE);

                    {
                        const MutexUnlock unlock(mStateMutex);
                        NOT_NULL(stream.plug)->OnConnect();
                    }

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
                DEBUG_MSG(
                        "duplicate src_port in VIRTIO_VSOCK_OP_REQUEST. "
                        "hdr={src_port=%u, dst_port=%u}}",
                        hdr.src_port, hdr.dst_port);
                return true;
            }
        } else {
            DEBUG_MSG("no listener for dst_port=%u", hostPort);
            return false;
        }
    }

    static struct virtio_vsock_hdr preparePacketHeader(const uint32_t srcPort,
                                                       const uint32_t dstPort,
                                                       const enum virtio_vsock_op op,
                                                       const uint32_t hostFwdCnt,
                                                       const uint32_t len) {
        constexpr uint32_t kHostBufAllocSize = 64 * 1024;

        // the rest of fields are set in virtio_vsock_send_packet_host_to_guest
        struct virtio_vsock_hdr hdr = {
            .src_port = srcPort,
            .dst_port = dstPort,
            .len = len,
            .op = static_cast<uint16_t>(op),
            .buf_alloc = kHostBufAllocSize,
            .fwd_cnt = hostFwdCnt,
        };

        return hdr;
    }

    static struct virtio_vsock_hdr preparePacketHeader(const VsockStream& stream,
                                                       const enum virtio_vsock_op op,
                                                       const uint32_t len) {
        return preparePacketHeader(stream.hostPort, stream.guestPort, op, stream.hostFwdCnt, len);
    }

    void queueOrphanPacketLocked(const struct virtio_vsock_hdr& hdr)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        mOrphanPackets.push_back(hdr);
    }

    void queueOrphanPacketLocked(const VsockStream& stream, const enum virtio_vsock_op op)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        queueOrphanPacketLocked(preparePacketHeader(stream, op, 0));
    }

    void queueOrphanPacketLocked(const struct virtio_vsock_hdr& request,
                                 const enum virtio_vsock_op op)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        queueOrphanPacketLocked(preparePacketHeader(request.dst_port, request.src_port, op, 0, 0));
    }

    void clearLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        for (const VsockStream& stream : mStreams) {
            if (stream.plug) {
                const MutexUnlock unlock(mStateMutex);
                stream.plug->OnUnplug().release();
            }
        }

        mStreams.clear();
        mHostEvents.clear();
        mOrphanPackets.clear();
        mSrcPortAllocator.Reset();
        mReturnedHostPorts.clear();
        mTickCounter = 0;
    }

    void clear() {
        const absl::MutexLock lock(mStateMutex);
        clearLocked();
    }

    void realize(void* const dev, const GoldfishVirtIOVSockDevAPI* const devApi) {
        DEBUG_MSG("this=%p, dev=%p, devApi=%p", this, dev, devApi);

        const absl::MutexLock lock(mStateMutex);
        mQemuDev = NOT_NULL(dev);
        mQemuDevApi = NOT_NULL(devApi);
    }

    void unrealize() {
        DEBUG_MSG("this=%p", this);
        clear();
        mQemuDev = nullptr;
        mQemuDevApi = nullptr;
    }

    void setStatus(const uint8_t status) {
        DCHECK(!(status & VIRTIO_CONFIG_S_FAILED));

        if (status & VIRTIO_CONFIG_S_NEEDS_RESET) {
            DEBUG_MSG("this=%p, status=S_NEEDS_RESET", this);
            clear();
        } else if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
            DEBUG_MSG("this=%p, status=S_DRIVER_OK", this);
        } else {
            DEBUG_MSG("this=%p, status=0x%02X", this, status);
        }
    }

    void onPacketReceiveControl(const struct virtio_vsock_hdr& hdr) {
        const absl::MutexLock lock(mStateMutex);
        if (hdr.op == VIRTIO_VSOCK_OP_REQUEST) {
            if (!processPacketOpRequestLocked(hdr)) {
                queueOrphanPacketLocked(hdr, VIRTIO_VSOCK_OP_RST);
            }
        } else {
            const VsockStreamKey key(hdr.src_port, hdr.dst_port);
            const auto streamI = mStreams.find(key);

            if (streamI != mStreams.end()) {
                VsockStream& stream = const_cast<VsockStream&>(*streamI);
                stream.guestBufAlloc = hdr.buf_alloc;
                stream.guestFwdCnt = hdr.fwd_cnt;

                switch (hdr.op) {
                case VIRTIO_VSOCK_OP_RESPONSE:
                    stream.isConnected = true;

                    {
                        const MutexUnlock unlock(mStateMutex);
                        NOT_NULL(stream.plug)->OnConnect();
                    }
                    break;

                case VIRTIO_VSOCK_OP_RST:
                    recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_INVALID);
                    mStreams.erase(streamI);
                    break;

                case VIRTIO_VSOCK_OP_SHUTDOWN:
                    recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_SHUTDOWN);
                    mStreams.erase(streamI);
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
                DEBUG_MSG("unexpected packet {src=%u, dst=%u, len=%u, op=%u}", hdr.src_port,
                          hdr.dst_port, hdr.len, hdr.op);
                queueOrphanPacketLocked(hdr, VIRTIO_VSOCK_OP_RST);
            }
        }
    }

    void* onPacketReceiveRwStart(const struct virtio_vsock_hdr& hdr)
            ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true, mStateMutex) {
        const VsockStreamKey key(hdr.src_port, hdr.dst_port);

        mStateMutex.lock();  // see onPacketReceiveRwEnd for unlock
        const auto streamI = mStreams.find(key);
        if (streamI != mStreams.end()) {
            VsockStream& stream = const_cast<VsockStream&>(*streamI);
            stream.guestBufAlloc = hdr.buf_alloc;
            stream.guestFwdCnt = hdr.fwd_cnt;
            return &stream;
        } else {
            // onPacketReceiveRwEnd is not required if there is no stream
            mStateMutex.unlock();
            return nullptr;
        }
    }

    // see onPacketReceiveRwStart and onPacketReceiveRwEnd
    int onPacketReceiveRw(void* streamPtr, const void* data, const size_t size)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        VsockStream& stream = *static_cast<VsockStream*>(streamPtr);

        if (stream.isConnected) {
            if (stream.dataSniffer) {
                stream.dataSniffer->ToPlug(data, size);
            }

            bool keepOpen;
            {
                const MutexUnlock unlock(mStateMutex);
                keepOpen = NOT_NULL(stream.plug)->OnReceive(data, size);
            }

            if (keepOpen) {
                stream.hostFwdCnt += size;
                stream.sendOp(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
                return 0;
            }
        }

        return 1;
    }

    void onPacketReceiveRwEnd(void* streamPtr, const int eraseStream)
            ABSL_UNLOCK_FUNCTION(mStateMutex) {
        if (eraseStream) {
            VsockStream& stream = *static_cast<VsockStream*>(streamPtr);
            recycleStreamLocked(stream, true, VIRTIO_VSOCK_OP_SHUTDOWN);
            mStreams.erase(stream);
        }
        onTick();
        mStateMutex.unlock();  // see onPacketReceiveRwStart
    }

    int sendPacketsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        DEBUG_MSG("this=%p", this);

        bool needNotify = false;
        VirtIOVSockSendResult sendResult;
        const auto sendPacketHostToGuest = NOT_NULL(mQemuDevApi)->sendPacketHostToGuest;

        while (!mOrphanPackets.empty()) {
            sendResult = (*NOT_NULL(sendPacketHostToGuest))(NOT_NULL(mQemuDev),
                                                            &mOrphanPackets.front(), nullptr);
            if (VirtIOVSockSendNeedNotify(sendResult)) {
                needNotify = true;
            }

            if (VirtIOVSockSendIsVqFull(sendResult)) {
                return needNotify;
            } else {
                mOrphanPackets.pop_front();
            }
        }

        for (const VsockStream& cStream : mStreams) {
            VsockStream& stream = const_cast<VsockStream&>(cStream);
            size_t guestAvailSize =
                    stream.guestBufAlloc - (stream.hostSentCnt - stream.guestFwdCnt);
            unsigned sendOpMask =
                    stream.sendOpMask |
                    ((guestAvailSize == 0) ? (1U << VIRTIO_VSOCK_OP_CREDIT_REQUEST) : 0);

            if (sendOpMask) {
                static const enum virtio_vsock_op ops[] = {
                    VIRTIO_VSOCK_OP_REQUEST,
                    VIRTIO_VSOCK_OP_RESPONSE,
                    VIRTIO_VSOCK_OP_CREDIT_UPDATE,
                    VIRTIO_VSOCK_OP_CREDIT_REQUEST,
                };

                for (const auto op : ops) {
                    if (sendOpMask & (1U << op)) {
                        auto hdr = preparePacketHeader(stream, op, 0);
                        sendResult = (*NOT_NULL(sendPacketHostToGuest))(NOT_NULL(mQemuDev), &hdr,
                                                                        nullptr);
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
                const auto [data, chunkSize] = stream.hostToGuestBuf.Peek();
                if (chunkSize == 0) {
                    break;
                }

                const size_t sendSize = std::min(chunkSize, guestAvailSize);
                auto hdr = preparePacketHeader(stream, VIRTIO_VSOCK_OP_RW, sendSize);
                sendResult = (*NOT_NULL(sendPacketHostToGuest))(NOT_NULL(mQemuDev), &hdr, data);

                const size_t sentSize = VirtIOVSockSentSize(sendResult);
                if (stream.hostToGuestBuf.Consume(sentSize) < stream.kBufferSizeLowWatermark) {
                    stream.setProducerEnabled(true);
                }

                stream.hostSentCnt += sentSize;
                guestAvailSize -= sentSize;

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

    int sendPackets() {
        const absl::MutexLock lock(mStateMutex);
        return sendPacketsLocked();
    }

    void sendPacketsAndNotifyLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mStateMutex) {
        if (sendPacketsLocked()) {
            (*NOT_NULL(mQemuDevApi)->haveHostToGuestPackets)(NOT_NULL(mQemuDev));
        }
    }

    int sendEvents() {
        DEBUG_MSG("this=%p", this);
        bool needNotify = false;

        const absl::MutexLock lock(mStateMutex);
        // TODO
        return needNotify;
    }

    int saveToSnapshot(IWriter& writer) const {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateSave) {
            r = (*mParentStateSave)(mParentStateArg, writer);
            if (r) {
                return r;
            }
        }

        const absl::MutexLock lock(mStateMutex);
        writer << mTickCounter << mReturnedHostPorts << mSrcPortAllocator << mOrphanPackets;

        writer << mStreams.size();
        for (const VsockStream& stream : mStreams) {
            writer << stream.guestPort << stream.hostPort << stream.hostFwdCnt;

            DCHECK(stream.plug);
            const IPlug& plug = *NOT_NULL(stream.plug);
            const bool supportsLoading = plug.SupportsLoadingFromSnapshot();
            writer << supportsLoading;
            if (supportsLoading) {
                DCHECK(!stream.dataSniffer && "dataSniffer is not snapshottable yet");

                const unsigned flags = (stream.isConnected ? 1U : 0U) | stream.sendOpMask;

                writer << stream.guestBufAlloc << stream.guestFwdCnt << stream.hostSentCnt << flags;

                stream.hostToGuestBuf.SaveToSnapshot(writer);

                if (!SavePlugToSnapshot(plug, writer)) {
                    return 1;
                }
            }
        }

        return 0;
    }

    int loadFromSnapshot(IReader& reader) {
        DEBUG_MSG("this=%p", this);
        int r;
        if (mParentStateLoad) {
            r = (*mParentStateLoad)(mParentStateArg, reader);
            if (r) {
                return r;
            }
        }

        const absl::MutexLock lock(mStateMutex);
        clearLocked();

        if (!ReadValue(reader, mTickCounter, mReturnedHostPorts, mSrcPortAllocator, mOrphanPackets)
                     .ok()) {
            return 1;
        }

        size_t n = 0;
        if (!ReadValue(reader, n).ok()) {
            return 1;
        }

        bool needNotify = false;
        for (; n > 0; --n) {
            uint32_t guestPort;
            uint32_t hostPort;
            uint32_t hostFwdCnt;
            bool supportsLoading;

            if (!ReadValue(reader, guestPort, hostPort, hostFwdCnt, supportsLoading).ok()) {
                return 1;
            }

            if (supportsLoading) {
                const auto [streamI, inserted] = mStreams.emplace(*this, guestPort, hostPort);
                if (!inserted) {
                    return 1;
                }

                VsockStream& stream = const_cast<VsockStream&>(*streamI);
                stream.hostFwdCnt = hostFwdCnt;

                uint8_t flags;
                if (!ReadValue(reader, stream.guestBufAlloc, stream.guestFwdCnt, stream.hostSentCnt,
                               flags)
                             .ok()) {
                    return 1;
                }

                stream.isConnected = (flags & 1U) != 0;
                stream.sendOpMask = flags & ~1U;
                stream.hostToGuestBuf.LoadFromSnapshot(reader);
                stream.producerEnabled = true;

                if (std::visit(PlugOrSocketVisitor(stream),
                               LoadPlugFromSnapshot(SocketPtr(&stream), reader))) {
                    return true;
                } else {
                    mStreams.erase(streamI);
                    return 1;
                }
            } else {
                // this stream does not support loading from
                // a snapshot, send RST to the guest
                queueOrphanPacketLocked(preparePacketHeader(hostPort, guestPort,
                                                            VIRTIO_VSOCK_OP_RST, hostFwdCnt, 0));
                needNotify = true;
            }
        }

        if (needNotify) {
            sendPacketsAndNotifyLocked();
        }

        return 0;
    }

    static GoldfishVirtioVsockDevice& from(void* ptr) {
        return *static_cast<GoldfishVirtioVsockDevice*>(ptr);
    }

    static const GoldfishVirtioVsockDevice& from(const void* ptr) {
        return *static_cast<const GoldfishVirtioVsockDevice*>(ptr);
    }

    struct VsockStreamComparer {
        using is_transparent = void;

        bool operator()(const VsockStream& lhs, const VsockStream& rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) < std::tie(rhs.guestPort, rhs.hostPort);
        }

        bool operator()(const VsockStreamKey& lhs, const VsockStream& rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) < std::tie(rhs.guestPort, rhs.hostPort);
        }

        bool operator()(const VsockStream& lhs, const VsockStreamKey& rhs) const {
            return std::tie(lhs.guestPort, lhs.hostPort) < std::tie(rhs.guestPort, rhs.hostPort);
        }
    };

    // IMPORTANT: std::set::erase
    // Other iterators and references are not invalidated.
    using Streams = std::set<VsockStream, VsockStreamComparer>;

    void* mQemuDev = nullptr;
    const GoldfishVirtIOVSockDevAPI* mQemuDevApi = nullptr;
    void* mParentStateArg = nullptr;
    int (*mParentStateSave)(const void*, IWriter&) = nullptr;
    int (*mParentStateLoad)(void*, IReader&) = nullptr;
    std::unordered_map<uint32_t, HostPortListener> mHostPortListeners ABSL_GUARDED_BY(mStateMutex);

    // Everything below is snapshotted
    std::deque<uint32_t> mReturnedHostPorts ABSL_GUARDED_BY(mStateMutex);
    goldfish::UniqueIdAllocator mSrcPortAllocator ABSL_GUARDED_BY(mStateMutex);
    std::deque<struct virtio_vsock_hdr> mOrphanPackets ABSL_GUARDED_BY(mStateMutex);
    std::deque<struct virtio_vsock_event> mHostEvents ABSL_GUARDED_BY(mStateMutex);
    Streams mStreams ABSL_GUARDED_BY(mStateMutex);
    uint16_t mTickCounter ABSL_GUARDED_BY(mStateMutex) = 0;

    mutable absl::Mutex mStateMutex;
};

///////////////////////////////////////////////////////////////////////////////
void VsockStream::SetOnFlowControlEvent(OnFlowControlEvent fce) {
    DCHECK(fce);
    onFlowControlEvent = std::move(fce);
}

void VsockStream::SendAsync(const void* data, size_t size) {
    DEBUG_MSG("this=%p vsockDev=%p size=%zu", this, &vsockDev, size);
    return vsockDev.sendAsyncImpl(*this, data, size);
}

PlugPtr VsockStream::UnplugImpl() {
    DEBUG_MSG("this=%p vsockDev=%p", this, &vsockDev);
    PlugPtr p = std::move(NOT_NULL(plug));
    vsockDev.unplugFromDevice(*this);  // calls ~VsockStream
    return p;
}
}  // namespace

namespace goldfish {
namespace vsock {
using devices::cable::IPlug;
using devices::cable::SocketPtr;

SocketPtr Connect(const uint32_t guestPort, PlugPtr plug) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.Connect(guestPort, std::move(NOT_NULL(plug)));
}

bool Listen(const uint32_t hostPort, HostPortListener listener) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.Listen(hostPort, std::move(listener));
}

void clear() {
    return GoldfishVirtioVsockDevice::getInstance().clear();
}

void setParentStateSnapshotHandlers(void* parent, int (*save)(const void*, archive::IWriter&),
                                    int (*load)(void*, archive::IReader&)) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    return instance.setParentStateSnapshotHandlers(parent, save, load);
}
}  // namespace vsock
}  // namespace goldfish

///////////////////////////////////////////////////////////////////////////////////////
void* goldfish_virtio_vsock_impl_realize(void* dev, const GoldfishVirtIOVSockDevAPI* devApi) {
    auto& instance = GoldfishVirtioVsockDevice::getInstance();
    instance.realize(NOT_NULL(dev), NOT_NULL(devApi));
    return &instance;
}

void goldfish_virtio_vsock_impl_unrealize(void* impl) {
    GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).unrealize();
}

void goldfish_virtio_vsock_set_status(void* impl, uint8_t status) {
    GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).setStatus(status);
}

void goldfish_virtio_vsock_accept_guest_to_host_control(void* impl,
                                                        const struct virtio_vsock_hdr* hdr) {
    GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).onPacketReceiveControl(*hdr);
}

void* goldfish_virtio_vsock_accept_guest_to_host_rw_start(
        void* impl, const struct virtio_vsock_hdr* hdr) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).onPacketReceiveRwStart(*hdr);
}

int goldfish_virtio_vsock_accept_guest_to_host_rw(void* impl, void* stream, const void* data,
                                                  size_t size) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).onPacketReceiveRw(stream, data, size);
}

void goldfish_virtio_vsock_accept_guest_to_host_rw_end(void* impl, void* stream, int erase_stream)
        ABSL_NO_THREAD_SAFETY_ANALYSIS {
    GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).onPacketReceiveRwEnd(stream, erase_stream);
}

int goldfish_virtio_vsock_handle_host_to_guest(void* impl) {
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).sendPackets();
}

int goldfish_virtio_vsock_handle_event_to_guest(void* impl) {
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).sendEvents();
}

int goldfish_virtio_vsock_impl_save(const void* impl, QEMUFile* f) {
    goldfish::archive::QEMUFileWriter writer(NOT_NULL(f));
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).saveToSnapshot(writer);
}

int goldfish_virtio_vsock_impl_load(void* impl, QEMUFile* f) {
    goldfish::archive::QEMUFileReader reader(NOT_NULL(f));
    return GoldfishVirtioVsockDevice::from(NOT_NULL(impl)).loadFromSnapshot(reader);
}

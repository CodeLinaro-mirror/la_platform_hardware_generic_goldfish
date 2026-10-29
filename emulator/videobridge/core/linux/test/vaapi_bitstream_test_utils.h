// Copyright (C) 2026 The Android Open Source Project
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

/**
 * @file vaapi_bitstream_test_utils.h
 * @brief Shared VA-API bitstream assembly reference helpers, test peer, segment-chain builder,
 *        and optional thread-local heap allocation tracker for unit tests and benchmarks.
 */

#if defined(__linux__)
#include <malloc.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "common_video/h264/h264_common.h"
#pragma clang diagnostic pop

#include "core/linux/va_api_types.h"
#include "core/linux/vaapi_video_encoder.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
        __has_feature(memory_sanitizer)
#define VAAPI_TEST_SANITIZER_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define VAAPI_TEST_SANITIZER_ACTIVE 1
#endif

#if defined(__linux__) && !defined(VAAPI_TEST_SANITIZER_ACTIVE)
#define VAAPI_TEST_ENABLE_HEAP_TRACKER 1
#endif

namespace goldfish::videobridge {

#if defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)

struct HeapAllocStats {
    size_t alloc_count = 0U;
    size_t free_count = 0U;
    size_t total_requested_bytes = 0U;
    size_t transient_freed_bytes = 0U;
    size_t live_usable_bytes = 0U;
    size_t peak_live_usable_bytes = 0U;
};

inline thread_local bool g_vaapi_heap_tracking_enabled = false;
inline thread_local HeapAllocStats g_vaapi_heap_stats{};

class ScopedHeapTracker {
  public:
    ScopedHeapTracker() {
        g_vaapi_heap_stats = HeapAllocStats{};
        g_vaapi_heap_tracking_enabled = true;
    }

    ~ScopedHeapTracker() { g_vaapi_heap_tracking_enabled = false; }

    ScopedHeapTracker(const ScopedHeapTracker&) = delete;
    ScopedHeapTracker& operator=(const ScopedHeapTracker&) = delete;

    HeapAllocStats Snapshot() const { return g_vaapi_heap_stats; }
};

inline void* AllocateVaapiTracked(size_t size, bool allow_null) {
    void* ptr = std::malloc(size == 0U ? 1U : size);
    if (ptr == nullptr) {
        if (allow_null) {
            return nullptr;
        }
#if defined(__cpp_exceptions)
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    if (g_vaapi_heap_tracking_enabled) {
        const size_t usable = malloc_usable_size(ptr);
        ++g_vaapi_heap_stats.alloc_count;
        g_vaapi_heap_stats.total_requested_bytes += size;
        g_vaapi_heap_stats.live_usable_bytes += usable;
        if (g_vaapi_heap_stats.live_usable_bytes > g_vaapi_heap_stats.peak_live_usable_bytes) {
            g_vaapi_heap_stats.peak_live_usable_bytes = g_vaapi_heap_stats.live_usable_bytes;
        }
    }
    return ptr;
}

inline void FreeVaapiTracked(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    if (g_vaapi_heap_tracking_enabled) {
        const size_t usable = malloc_usable_size(ptr);
        ++g_vaapi_heap_stats.free_count;
        g_vaapi_heap_stats.transient_freed_bytes += usable;
        g_vaapi_heap_stats.live_usable_bytes =
                (g_vaapi_heap_stats.live_usable_bytes >= usable)
                        ? (g_vaapi_heap_stats.live_usable_bytes - usable)
                        : 0U;
    }
    std::free(ptr);
}

#define VAAPI_DEFINE_HEAP_TRACKER_NEW_DELETE()                                            \
    void* operator new(size_t size) {                                                     \
        return ::goldfish::videobridge::AllocateVaapiTracked(size, /*allow_null=*/false); \
    }                                                                                     \
    void* operator new[](size_t size) {                                                   \
        return ::goldfish::videobridge::AllocateVaapiTracked(size, /*allow_null=*/false); \
    }                                                                                     \
    void* operator new(size_t size, const std::nothrow_t&) noexcept {                     \
        return ::goldfish::videobridge::AllocateVaapiTracked(size, /*allow_null=*/true);  \
    }                                                                                     \
    void* operator new[](size_t size, const std::nothrow_t&) noexcept {                   \
        return ::goldfish::videobridge::AllocateVaapiTracked(size, /*allow_null=*/true);  \
    }                                                                                     \
    void operator delete(void* ptr) noexcept {                                            \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }                                                                                     \
    void operator delete[](void* ptr) noexcept {                                          \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }                                                                                     \
    void operator delete(void* ptr, size_t) noexcept {                                    \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }                                                                                     \
    void operator delete[](void* ptr, size_t) noexcept {                                  \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }                                                                                     \
    void operator delete(void* ptr, const std::nothrow_t&) noexcept {                     \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }                                                                                     \
    void operator delete[](void* ptr, const std::nothrow_t&) noexcept {                   \
        ::goldfish::videobridge::FreeVaapiTracked(ptr);                                   \
    }

#else  // !defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)

#define VAAPI_DEFINE_HEAP_TRACKER_NEW_DELETE()

#endif  // defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)

class VaapiVideoEncoderTestPeer {
  public:
    static webrtc::scoped_refptr<webrtc::EncodedImageBuffer> AssembleEncodedBitstream(
            const void* coded_data, bool force_idr, VAProfile profile,
            absl::Span<const uint8_t> sps_nalu, absl::Span<const uint8_t> pps_nalu,
            std::vector<uint8_t>& scratch_bitstream) {
        return VaapiVideoEncoder::AssembleEncodedBitstream(coded_data, force_idr, profile, sps_nalu,
                                                           pps_nalu, scratch_bitstream);
    }

    static size_t ScratchBitstreamCapacity(const VaapiVideoEncoder& encoder) {
        absl::MutexLock lock(&encoder.mutex_);
        return encoder.scratch_bitstream_.capacity();
    }
};

struct ReferenceAnnexBStartCode {
    size_t start_idx;
    size_t header_idx;
};

inline std::optional<ReferenceAnnexBStartCode> FindReferenceAnnexBStartCode(
        const std::vector<uint8_t>& bitstream, size_t offset) {
    for (size_t i = offset; i + webrtc::H264::kNaluShortStartSequenceSize < bitstream.size(); ++i) {
        if (bitstream[i] != 0x00U || bitstream[i + 1U] != 0x00U) {
            continue;
        }
        if (bitstream[i + 2U] == 0x01U) {
            return ReferenceAnnexBStartCode{i, i + webrtc::H264::kNaluShortStartSequenceSize};
        }
        if (i + webrtc::H264::kNaluLongStartSequenceSize < bitstream.size() &&
            bitstream[i + 2U] == 0x00U && bitstream[i + 3U] == 0x01U) {
            return ReferenceAnnexBStartCode{i, i + webrtc::H264::kNaluLongStartSequenceSize};
        }
    }
    return std::nullopt;
}

inline uint8_t ReferenceProfileToIdc(VAProfile profile) {
    switch (profile) {
    case VAProfileH264Main:
        return 77U;
    case VAProfileH264High:
        return 100U;
    case VAProfileH264ConstrainedBaseline:
    case VAProfileH264Baseline:
    default:
        return 66U;
    }
}

inline bool ReferenceHasMatchingSpsNalu(const std::vector<uint8_t>& bitstream,
                                        uint8_t expected_profile_idc) {
    for (size_t pos = 0U; auto sc = FindReferenceAnnexBStartCode(bitstream, pos);
         pos = sc->start_idx + 1U) {
        if (sc->header_idx + 1U < bitstream.size() &&
            webrtc::H264::ParseNaluType(bitstream[sc->header_idx]) ==
                    webrtc::H264::NaluType::kSps) {
            return bitstream[sc->header_idx + 1U] == expected_profile_idc;
        }
    }
    return false;
}

inline std::vector<uint8_t> ReferenceStripLeadingSpsPps(const std::vector<uint8_t>& bitstream) {
    for (size_t pos = 0U; auto sc = FindReferenceAnnexBStartCode(bitstream, pos);
         pos = sc->start_idx + 1U) {
        if (sc->header_idx < bitstream.size()) {
            const webrtc::H264::NaluType nal_type =
                    webrtc::H264::ParseNaluType(bitstream[sc->header_idx]);
            if (nal_type != webrtc::H264::NaluType::kSps &&
                nal_type != webrtc::H264::NaluType::kPps) {
                return std::vector<uint8_t>(
                        bitstream.begin() + static_cast<ptrdiff_t>(sc->start_idx), bitstream.end());
            }
        }
    }
    return bitstream;
}

/**
 * @brief Pre-optimization reference implementation of coded-buffer bitstream assembly using
 *        intermediate std::vector buffers, preserved for differential equivalence tests and
 *        baseline memory/throughput benchmarking.
 */
inline webrtc::scoped_refptr<webrtc::EncodedImageBuffer> AssembleEncodedBitstreamBaseline(
        const void* coded_data, bool force_idr, VAProfile profile,
        absl::Span<const uint8_t> sps_nalu, absl::Span<const uint8_t> pps_nalu) {
    std::vector<uint8_t> bitstream;
    const auto* segment = static_cast<const VACodedBufferSegment*>(coded_data);
    while (segment != nullptr) {
        if (segment->buf != nullptr && segment->size > 0U) {
            const auto* bytes = static_cast<const uint8_t*>(segment->buf);
            bitstream.insert(bitstream.end(), bytes, bytes + segment->size);
        }
        segment = static_cast<const VACodedBufferSegment*>(segment->next);
    }

    if (bitstream.empty()) {
        const size_t fallback_size = force_idr ? 31U : 11U;
        const auto* bytes = static_cast<const uint8_t*>(coded_data);
        bitstream.assign(bytes, bytes + fallback_size);
    }

    if (force_idr && !ReferenceHasMatchingSpsNalu(bitstream, ReferenceProfileToIdc(profile))) {
        std::vector<uint8_t> slice_only = ReferenceStripLeadingSpsPps(bitstream);
        std::vector<uint8_t> full_idr;
        full_idr.reserve(sps_nalu.size() + pps_nalu.size() + slice_only.size());
        full_idr.insert(full_idr.end(), sps_nalu.begin(), sps_nalu.end());
        full_idr.insert(full_idr.end(), pps_nalu.begin(), pps_nalu.end());
        full_idr.insert(full_idr.end(), slice_only.begin(), slice_only.end());
        bitstream = std::move(full_idr);
    }

    return webrtc::EncodedImageBuffer::Create(bitstream.data(), bitstream.size());
}

inline std::vector<VACodedBufferSegment> LinkCodedSegments(
        std::vector<std::vector<uint8_t>>& chunks) {
    std::vector<VACodedBufferSegment> nodes(chunks.size(), VACodedBufferSegment{});
    for (size_t i = 0U; i < chunks.size(); ++i) {
        nodes[i].size = static_cast<uint32_t>(chunks[i].size());
        nodes[i].buf = chunks[i].empty() ? nullptr : chunks[i].data();
        nodes[i].next = (i + 1U < chunks.size()) ? &nodes[i + 1U] : nullptr;
    }
    return nodes;
}

}  // namespace goldfish::videobridge

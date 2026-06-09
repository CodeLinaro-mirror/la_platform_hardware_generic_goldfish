// Copyright 2026 The Android Open Source Project
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

/**
 * @file breadcrumb_proto.h
 * @brief High-performance breadcrumb tracing system with binary envelopes.
 *
 * This header defines the infrastructure for logging diagnostic breadcrumbs
 * in hot paths of the emulator (e.g., gRPC, ADB, and potentially Virtio).
 *
 * @section rationale Rationale
 * Benchmarks demonstrated that standard Protobuf serialization can introduce
 * significant overhead (~40ns per event) in hyper-hot paths. By switching to
 * a custom binary envelope, we achieved a ~17x speedup (~2.5ns per event),
 * making it safe to inject breadcrumbs without distorting timing or causing
 * performance degradation.
 *
 * @section format Format & Alignment
 * Every breadcrumb consists of a fixed-size 32-byte record (2-byte header +
 * 30-byte binary envelope) followed by a variable-length payload.
 *
 * The `BreadcrumbEnvelope` struct is explicitly packed using `__attribute__((packed))`
 * to prevent compiler-inserted padding and ensure cross-architecture compatibility.
 * The envelope is exactly 30 bytes so that, when combined with the 2-byte
 * RawCircularLog::ObjectHeader, the total entry size is exactly 32 bytes.
 * This ensures that every entry and its internal 64-bit fields are perfectly
 * 8-byte aligned, preventing cache-line splits and atomic faults.
 *
 * Downstream consumers (like the `crashreport` tool) must use unaligned-safe
 * access methods (like `std::memcpy`) when reading these fields if they cannot
 * guarantee the alignment of the source buffer.
 *
 * Payloads can be either serialized Protobuf messages (for flexibility) or
 * raw binary structs (for maximum speed), identified by the `payload_type` field.
 */

#pragma once
#include <cstdint>
#include <string_view>

#include "absl/status/status.h"

#include "goldfish/raw_circular_log.h"

namespace android::crashreport {

using goldfish::proto_data_store::RawCircularLog;

/**
 * @brief Identifies the subsystem that generated the breadcrumb.
 */
enum class BreadcrumbType {
    kGrpc,  ///< gRPC subsystem events.
    kAdb,   ///< ADB subsystem events.
};

/**
 * @brief Represents the phase of a breadcrumb event, useful for correlation.
 */
enum class BreadcrumbPhase : uint8_t {
    kInstant = 0,    ///< Point-in-time event without duration.
    kFlowBegin = 1,  ///< Start of a cross-thread operation.
    kFlowStep = 2,   ///< Intermediate step in a flow.
    kFlowEnd = 3,    ///< End of a cross-thread operation.
};

/**
 * @brief Common envelope header for all breadcrumbs.
 *
 * Every breadcrumb record in the circular buffer starts with this header.
 * It has a packed size of 28 bytes (30 bytes total in the circular log
 * when including the 2-byte object header).
 */
struct BreadcrumbEnvelope {
    uint64_t timestamp_ns;  ///< Monotonic timestamp in nanoseconds.
    uint64_t thread_id;     ///< OS Thread ID.
    uint64_t flow_id;       ///< For cross-thread correlation.
    uint8_t phase;          ///< BreadcrumbPhase enum value.
    uint8_t payload_type;   ///< PayloadType enum value.
    uint16_t payload_len;   ///< Length of the following payload.
} __attribute__((packed));

/**
 * @brief Raw binary payload for ADB events.
 *
 * This struct defines the fixed-size header for an ADB breadcrumb event.
 * To minimize overhead in hot paths, it does not use Protobuf serialization.
 * Instead, it uses a flat binary layout.
 *
 * @section serialization Serialization Format
 * The true serialization format in memory is:
 * [RawAdbPayload (5 bytes)] [data (snippet_len bytes)]
 *
 * The `data` field is not explicitly declared in the struct to remain strictly
 * compliant with standard C++ (avoiding flexible array members). Consumers
 * must use pointer arithmetic based on `sizeof(RawAdbPayload)` and `snippet_len`
 * to access the data payload.
 */
struct RawAdbPayload {
    uint32_t command;     ///< 4-character command packed as uint32 (e.g., 'CNXN').
    uint8_t snippet_len;  ///< Length of the following snippet data in bytes.
    // Followed by: char data[snippet_len];
} __attribute__((packed));

/**
 * @brief Identifies the serialization format of the payload following the envelope.
 */
enum class PayloadType : uint8_t {
    kGrpcProto = 1,  ///< Protobuf serialized GrpcPayload.
    kAdbProto = 2,   ///< Protobuf serialized AdbPayload (deprecated).
    kRaw = 3,        ///< Generic raw binary payload.
    kString = 4,     ///< Free-form string payload.
    kAdbRawToGuest = 5,
    kAdbRawToHost = 6,
};

// Returns the log instance for a specific breadcrumb type.
RawCircularLog* GetBreadcrumbLog(BreadcrumbType type);

// Helper to log a breadcrumb with a binary envelope.
absl::Status LogBreadcrumb(BreadcrumbType type, uint64_t flow_id, BreadcrumbPhase phase,
                           PayloadType payload_type, std::string_view payload);

/**
 * @brief Generates a thread-safe, globally unique flow identifier for cross-thread correlation.
 *
 * This flow ID is used to connect asynchronous operations, requests, or events
 * across thread and subsystem boundaries.
 *
 * @return A non-zero globally unique 64-bit flow identifier.
 */
uint64_t AllocateGlobalFlowId();

}  // namespace android::crashreport

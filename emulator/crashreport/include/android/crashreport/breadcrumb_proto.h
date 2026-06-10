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
 * Every breadcrumb consists of a fixed-size 30-byte record (2-byte ObjectHeader +
 * 28-byte binary envelope) followed by a variable-length payload, aligned to 8-byte
 * boundaries by the circular log.
 *
 * The `BreadcrumbEnvelope` struct is explicitly packed using `__attribute__((packed))`
 * to prevent compiler-inserted padding and ensure cross-architecture compatibility.
 * The envelope is 28 bytes. When combined with the 2-byte RawCircularLog::ObjectHeader,
 * the total header size is 30 bytes.
 *
 * Because of the 2-byte ObjectHeader, the envelope itself is stored at a 2-byte aligned
 * offset (8k + 2) relative to the start of the aligned log entry. Consequently, its
 * internal 64-bit fields are 2-byte aligned, not 8-byte aligned. Downstream consumers
 * must use unaligned-safe access methods (such as `std::memcpy`) when reading these fields
 * to prevent alignment faults or undefined behavior on strict-alignment architectures.
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

using ::goldfish::proto_data_store::RawCircularLog;

using TimestampNs = uint64_t;
using ThreadId = uint64_t;
using FlowId = uint64_t;

/**
 * @brief Fixed-size (64-bit) storage for stack return addresses.
 *
 * Guarantees a stable 8-byte layout for out-of-process crash dump parsing across
 * 32-bit and 64-bit architectures, while allowing direct implicit assignment from
 * void* compiler builtins like __builtin_return_address without explicit casting.
 */
struct StackAddress {
    uint64_t address{0};

    constexpr StackAddress() = default;
    StackAddress(decltype(__builtin_return_address(0)) ptr)
            : address(reinterpret_cast<uint64_t>(ptr)) {}
    constexpr StackAddress(uint64_t addr) : address(addr) {}
    constexpr StackAddress(int addr) : address(static_cast<uint64_t>(addr)) {}

    StackAddress& operator=(decltype(__builtin_return_address(0)) ptr) {
        address = reinterpret_cast<uint64_t>(ptr);
        return *this;
    }

    constexpr operator uint64_t() const { return address; }
} __attribute__((packed));

static_assert(sizeof(StackAddress) == sizeof(uint64_t),
              "StackAddress must be exactly 8 bytes packed.");
static_assert(sizeof(decltype(__builtin_return_address(0))) <= sizeof(StackAddress),
              "Return address pointer from __builtin_return_address must fit inside StackAddress");

/**
 * @brief Identifies the subsystem that generated the breadcrumb.
 */
enum class BreadcrumbType {
    kGrpc,    ///< gRPC subsystem events.
    kAdb,     ///< ADB subsystem events.
    kEvents,  ///< General emulator events (looper flows, lifecycle, etc.).
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
 * @brief Identifies the serialization format of the payload following the envelope.
 */
enum class PayloadType : uint8_t {
    kGrpcProto = 1,             ///< Protobuf serialized GrpcPayload.
    kAdbProto = 2,              ///< Protobuf serialized AdbPayload (deprecated).
    kRaw = 3,                   ///< Generic raw binary payload.
    kString = 4,                ///< Free-form string payload.
    kAdbRawToGuest = 5,         ///< Raw binary ADB message payload sent from host to guest.
    kAdbRawToHost = 6,          ///< Raw binary ADB message payload sent from guest to host.
    kLooperExecRaw = 7,         ///< RawLooperExecPayload.
    kLooperPostContextRaw = 8,  ///< RawLooperPostWithContextPayload.
};

/**
 * @brief Common envelope header for all breadcrumbs.
 *
 * Every breadcrumb record in the circular buffer starts with this header.
 * It has a packed size of 28 bytes (30 bytes total in the circular log
 * when including the 2-byte object header).
 */
struct BreadcrumbEnvelope {
    TimestampNs timestamp_ns;  ///< Monotonic timestamp in nanoseconds.
    ThreadId thread_id;        ///< OS Thread ID.
    FlowId flow_id;            ///< For cross-thread correlation.
    BreadcrumbPhase phase;     ///< BreadcrumbPhase enum value.
    PayloadType payload_type;  ///< PayloadType enum value.
    uint16_t payload_len;      ///< Length of the following payload.
} __attribute__((packed));

static_assert(sizeof(BreadcrumbEnvelope) == 28,
              "BreadcrumbEnvelope must be exactly 28 bytes packed. Changing this will break "
              "crash dump parsing between emulator versions.");

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
 * This template-based layout allows trackers to allocate variable-sized payloads
 * directly on the stack without heap allocation, ensuring zero-allocation logging:
 *
 * @code
 * RawAdbPayloadT<32> stack_payload;
 * stack_payload.command = command;
 * stack_payload.snippet_len = snippet_len;
 * std::memcpy(stack_payload.data, snippet_ptr, snippet_len);
 *
 * LogBreadcrumb(..., std::string_view(
 *     reinterpret_cast<const char*>(&stack_payload),
 *     sizeof(RawAdbPayload) + snippet_len));
 * @endcode
 *
 * Consumers of the `RawAdbPayload` alias can access the trailing payload bytes via
 * pointer arithmetic starting from the address of `raw_adb.data`.
 */
/**
 * @brief Raw binary payload template for ADB events.
 */
template <size_t N>
struct RawAdbPayloadT {
    uint32_t command;     ///< 4-character command packed as uint32 (e.g., 'CNXN').
    uint8_t snippet_len;  ///< Length of the following snippet data in bytes.
    char data[N];         ///< Variable length snippet data, copied inline.
} __attribute__((packed));

using RawAdbPayload = RawAdbPayloadT<0>;

/**
 * @brief Raw binary payload for looper task posting with context.
 *
 * This struct defines the fixed-size header for a looper task post event.
 * To minimize overhead in hot paths, it does not use Protobuf serialization.
 * Instead, it uses a flat binary layout.
 *
 * @section serialization Serialization Format
 * The true serialization format in memory is:
 * [RawLooperPostWithContextPayload (10 bytes)] [context_data (context_len bytes)]
 *
 * This template-based layout allows trackers to allocate variable-sized payloads
 * directly on the stack without heap allocation, ensuring zero-allocation logging:
 *
 * @code
 * RawLooperPostWithContextPayloadT<32> stack_payload;
 * stack_payload.caller_pc = caller_pc;
 * stack_payload.loop_id = loop_id;
 * stack_payload.context_len = context_len;
 * std::memcpy(stack_payload.context_data, context_ptr, context_len);
 *
 * LogBreadcrumb(..., std::string_view(
 *     reinterpret_cast<const char*>(&stack_payload),
 *     sizeof(RawLooperPostWithContextPayload) + context_len));
 * @endcode
 *
 * Consumers of the `RawLooperPostWithContextPayload` alias can access the trailing payload bytes
 * via pointer arithmetic starting from the address of `raw_looper.context_data`.
 */
template <size_t N>
struct RawLooperPostWithContextPayloadT {
    StackAddress caller_pc;  ///< Return address of the Post caller.
    uint8_t loop_id;         ///< Unique identifier of the target event loop.
    uint8_t context_len;     ///< Length of the context string.
    char context_data[N];    ///< Variable length context data, copied inline.
} __attribute__((packed));

using RawLooperPostWithContextPayload = RawLooperPostWithContextPayloadT<0>;

/**
 * @brief Raw binary payload for looper task execution.
 */
struct RawLooperExecPayload {
    uint8_t loop_id;  ///< Unique identifier of the executing event loop.
} __attribute__((packed));

/**
 * @brief Retrieves the raw circular log writer instance associated with a specific breadcrumb type.
 *
 * @param type The category of subsystem generating the breadcrumbs.
 * @return A pointer to the RawCircularLog instance, or nullptr if initialization failed.
 */
RawCircularLog* GetBreadcrumbLog(BreadcrumbType type);

/**
 * @brief Logs a diagnostic breadcrumb event using a standard binary envelope directly to a circular
 * log.
 *
 * Appends the event metadata (envelope) followed by the custom payload to the provided
 * circular buffer.
 *
 * @param log The destination raw circular log. Must not be null.
 * @param flow_id The unique identifier connecting steps in this cross-thread flow.
 * @param phase The execution phase of the flow step (Begin, Step, End, or Instant).
 * @param payload_type The serialization format identifier of the payload.
 * @param payload The serialized payload data.
 * @return absl::Status OkStatus on success, InvalidArgumentError if log is null, or an error status
 * on failure.
 */
absl::Status LogBreadcrumbTo(RawCircularLog* log, FlowId flow_id, BreadcrumbPhase phase,
                             PayloadType payload_type, std::string_view payload);

/**
 * @brief Logs a diagnostic breadcrumb event using a standard binary envelope.
 *
 * Appends the event metadata (envelope) followed by the custom payload to the appropriate
 * circular buffer based on the breadcrumb type.
 *
 * @param type The subsystem generating the breadcrumb.
 * @param flow_id The unique identifier connecting steps in this cross-thread flow.
 * @param phase The execution phase of the flow step (Begin, Step, End, or Instant).
 * @param payload_type The serialization format identifier of the payload.
 * @param payload The serialized payload data.
 * @return absl::Status OkStatus on success, InternalError if the log for type is not initialized,
 *                 InvalidArgumentError if type is invalid, or an error status on failure.
 */
absl::Status LogBreadcrumb(BreadcrumbType type, FlowId flow_id, BreadcrumbPhase phase,
                           PayloadType payload_type, std::string_view payload);

/**
 * @brief Generates a thread-safe, globally unique flow identifier for cross-thread correlation.
 *
 * This flow ID is used to connect asynchronous operations, requests, or events
 * across thread and subsystem boundaries.
 *
 * @return A non-zero globally unique 64-bit flow identifier.
 */
FlowId AllocateGlobalFlowId();

}  // namespace android::crashreport

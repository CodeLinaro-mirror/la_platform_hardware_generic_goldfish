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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "absl/status/status.h"

extern "C" {
typedef struct AudioBackend AudioBackend;
typedef struct CaptureVoiceOut CaptureVoiceOut;
}

namespace goldfish::audio {

/**
 * @class QemuAudioCapture
 * @brief Captures guest PCM audio from QEMU's AudioBackend and forwards it to an AudioCallback.
 *
 * @note **Threading Model:** This class lives and operates directly within the QEMU audio
 * subsystem. The registered @ref AudioCallback is invoked synchronously on the QEMU audio
 * mixing/vCPU thread for each buffer of captured PCM samples. Callbacks MUST NOT perform blocking
 * I/O, heavy CPU computation, or acquire long locks that could stall QEMU emulation. Higher-level
 * components (such as WebRTC sinks, audio encoders, or network streamers) are responsible for
 * marshalling audio packets to separate worker threads or event loops if non-trivial processing is
 * required.
 *
 * @note **Lifecycle & Invariant:** Capture state is governed solely by the atomic
 * @ref capture_voice_ pointer. The class strictly enforces the invariant:
 * @code capture_voice_ != nullptr @endcode if and only if audio capture is active and running.
 */
class QemuAudioCapture {
  public:
    using AudioCallback = std::function<void(const int16_t* pcm_data, size_t num_samples)>;

    /**
     * @brief Constructs an audio capture pipeline with a callback and format.
     *
     * @param callback Callback invoked synchronously on the QEMU audio thread for each chunk of PCM
     * samples.
     * @param sample_rate_hz Sample rate in Hz (default: 48000).
     * @param channels Channel count (default: 2).
     * @param audio_backend Optional custom QEMU AudioBackend. When omitted or nullptr, the active
     * backend (default or "mainaudiodev") is resolved automatically during construction.
     */
    explicit QemuAudioCapture(AudioCallback callback, uint32_t sample_rate_hz = 48000,
                              uint32_t channels = 2, AudioBackend* audio_backend = nullptr);

    ~QemuAudioCapture();

    /**
     * @brief Starts capturing audio from the QEMU audio backend.
     *
     * Registers a capture voice with the QEMU audio subsystem under the VM lock.
     * At most one active capture voice is maintained at any time. If capture is already
     * running, subsequent calls to Start() are idempotent no-ops and return absl::OkStatus().
     *
     * @return absl::OkStatus() on success, or an error status if the audio backend
     * is unavailable or voice registration fails.
     */
    absl::Status Start();

    /**
     * @brief Stops audio capture and unregisters the capture voice from QEMU.
     *
     * Atomically clears the active capture voice handle and unregisters it under the VM lock.
     * Thread-safe and safe to call concurrently or repeatedly from any number of threads as often
     * as needed; only the first call unregisters the voice, while subsequent/concurrent calls are
     * no-ops.
     */
    void Stop();

    /**
     * @brief Checks if audio capture is currently active (invariant: @c capture_voice_ != nullptr).
     *
     * @return true if capturing audio, false otherwise.
     */
    bool IsRunning() const { return capture_voice_.load(std::memory_order_relaxed) != nullptr; }

    /**
     * @brief Returns the configured sample rate in Hz.
     */
    uint32_t sample_rate_hz() const { return sample_rate_hz_; }

    /**
     * @brief Returns the configured channel count.
     */
    uint32_t channels() const { return channels_; }

  private:
    /**
     * @brief C-compatible trampoline callback invoked by QEMU's audio subsystem.
     */
    static void OnCaptureStatic(void* opaque, const void* buf, int size);

    /**
     * @brief Internal handler that validates and forwards captured PCM buffers to the registered
     * callback.
     */
    void OnCapture(const void* buf, int size);

    const AudioCallback callback_;       ///< Callback for delivering PCM samples.
    AudioBackend* const audio_backend_;  ///< QEMU audio backend to capture from.
    std::atomic<CaptureVoiceOut*> capture_voice_{
        nullptr};  ///< Active QEMU capture voice. Invariant: non-null iff running.
    const uint32_t sample_rate_hz_;  ///< Sample rate in Hz.
    const uint32_t channels_;        ///< Channel count.
};

}  // namespace goldfish::audio

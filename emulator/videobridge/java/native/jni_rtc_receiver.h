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

#include <jni.h>

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#pragma clang diagnostic pop

#include "frame_buffer_pool.h"
#include "jni_video_sink.h"
#include "nlohmann/json.hpp"

/**
 * @file jni_rtc_receiver.h
 * @brief Native WebRTC PeerConnection receiver wrapper for JNI integration with Java/Kotlin
 * applications.
 */

namespace goldfish::videobridge::java {

/**
 * @class JniRtcReceiver
 * @brief Manages a native WebRTC PeerConnection lifecycle and bridges events to Java/Kotlin
 * callbacks.
 *
 * `JniRtcReceiver` owns the dedicated WebRTC threads (network, worker, signaling), creates and
 * manages the underlying `webrtc::PeerConnection`, receives remote video tracks via `JniVideoSink`,
 * manages the input data channel, and marshals JSEP signaling JSON messages back and forth to Java
 * listeners via JNI.
 */
class JniRtcReceiver : public webrtc::PeerConnectionObserver,
                       public std::enable_shared_from_this<JniRtcReceiver> {
  public:
    /**
     * @brief Constructs a new JniRtcReceiver instance.
     * @param jvm Pointer to the Java Virtual Machine.
     * @param format Desired pixel format for decoded video frames passed to Java.
     */
    JniRtcReceiver(JavaVM* jvm, PixelFormat format);

    /** @brief Destructor. Closes PeerConnection and terminates WebRTC worker threads cleanly. */
    ~JniRtcReceiver() override;

    /**
     * @brief Initializes WebRTC worker threads, PeerConnectionFactory, and PeerConnection.
     * @return True if initialization succeeded; false otherwise.
     */
    bool Initialize();

    /**
     * @brief Sets the Java video frame listener callback object.
     * @param env JNI environment pointer.
     * @param listener Global/local reference to a Java `VideoFrameListener` object.
     */
    void SetVideoFrameListener(JNIEnv* env, jobject listener);

    /**
     * @brief Sets the Java JSEP message listener callback object.
     * @param env JNI environment pointer.
     * @param listener Global/local reference to a Java `JsepMessageListener` object.
     */
    void SetJsepMessageListener(JNIEnv* env, jobject listener);

    /**
     * @brief Sets the Java connection state listener callback object.
     * @param env JNI environment pointer.
     * @param listener Global/local reference to a Java `ConnectionStateListener` object.
     */
    void SetConnectionStateListener(JNIEnv* env, jobject listener);

    /**
     * @brief Initiates asynchronous creation of a local SDP offer.
     */
    void CreateOffer();

    /**
     * @brief Processes an incoming JSEP message (SDP answer/offer or ICE candidate) from Video
     * Bridge.
     * @param env JNI environment pointer.
     * @param jsep_json Raw JSON string containing JSEP payload.
     */
    void AcceptJsepMessage(JNIEnv* env, const std::string& jsep_json);

    /**
     * @brief Sends a touch input event over the WebRTC input data channel.
     *
     * Supports multi-touch by associating each touch gesture with a unique point identifier.
     * Pressure is automatically set to 0 when UP (2) or CANCEL (3) action is sent to signal touch
     * release.
     *
     * @param id Touch point identifier for tracking multi-touch gestures (e.g., 0, 1, 2).
     * @param x Touch X coordinate in pixels.
     * @param y Touch Y coordinate in pixels.
     * @param action_enum Touch action (DOWN = 0, MOVE = 1, UP = 2, CANCEL = 3).
     */
    void SendTouchEvent(int id, int x, int y, int action_enum);

    /**
     * @brief Sends a mouse input event over the WebRTC input data channel.
     * @param x Mouse X coordinate in pixels.
     * @param y Mouse Y coordinate in pixels.
     * @param button_enum Mouse button (NONE = 0, LEFT = 1, RIGHT = 2, MIDDLE = 3).
     * @param action_enum Mouse action ordinal.
     */
    void SendMouseEvent(int x, int y, int button_enum, int action_enum);

    /**
     * @brief Sends a keyboard input event over the WebRTC input data channel.
     *
     * The \p key parameter expects a W3C standard `KeyboardEvent.key` string value
     * (e.g., "a", "A", "Enter", "ArrowUp", "Backspace", "Escape"). Printable ASCII characters
     * and standard key identifiers are translated to corresponding evdev events by the emulator.
     *
     * Special Android system key strings are also supported:
     * - "GoBack": Android Back button
     * - "GoHome": Android Home button
     * - "AppSwitch": Android Overview / Recents button
     * - "Power": Android Power button
     *
     * @param key W3C `KeyboardEvent.key` string representation (e.g., "a", "Enter", "GoBack").
     * @param down True for key down events; false for key up events.
     */
    void SendKeyEvent(const std::string& key, bool down);

    // webrtc::PeerConnectionObserver overrides
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {}
    void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}
    void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

    /**
     * @brief Callback invoked when a remote data channel is created.
     * @param data_channel Pointer to the created data channel.
     */
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;

    void OnRenegotiationNeeded() override {}

    /**
     * @brief Callback invoked when the ICE connection state changes.
     * @param new_state New ICE connection state.
     */
    void OnIceConnectionChange(
            webrtc::PeerConnectionInterface::IceConnectionState new_state) override;

    void OnIceGatheringChange(
            webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}

    /**
     * @brief Callback invoked when a local ICE candidate is gathered.
     * @param candidate Pointer to the gathered ICE candidate.
     */
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;

    /**
     * @brief Callback invoked when a remote media track (e.g. video) is added to the
     * PeerConnection.
     * @param transceiver Transceiver containing the received track.
     */
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

    /**
     * @brief Callback invoked when local SDP offer creation succeeds.
     * @param desc Pointer to the created session description.
     */
    void OnSessionDescriptionCreated(webrtc::SessionDescriptionInterface* desc);

    /**
     * @brief Callback invoked when local SDP offer creation fails.
     * @param error RTC error details.
     */
    void OnSessionDescriptionFailure(webrtc::RTCError error);

  private:
    /**
     * @brief Attaches the current native thread to the JVM if needed and returns the JNIEnv
     * pointer.
     * @param[out] out_attached Set to true if the thread was attached by this call and must be
     * detached later.
     * @return Pointer to JNIEnv, or nullptr on failure.
     */
    JNIEnv* GetJniEnv(bool* out_attached);

    /**
     * @brief Dispatches an outbound JSEP JSON message to the Java JSEP message listener.
     * @param json_msg JSON object representing the outbound JSEP message.
     */
    void DispatchOutboundJsep(const nlohmann::json& json_msg);

    JavaVM* jvm_;
    PixelFormat target_format_;
    std::shared_ptr<FrameBufferPool> pool_;
    std::shared_ptr<JniVideoSink> video_sink_;

    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    webrtc::scoped_refptr<webrtc::DataChannelInterface> input_data_channel_;

    absl::Mutex listeners_mutex_;
    jobject jsep_listener_ref_ ABSL_GUARDED_BY(listeners_mutex_) = nullptr;
    jmethodID on_outbound_jsep_mid_ ABSL_GUARDED_BY(listeners_mutex_) = nullptr;
    jobject state_listener_ref_ ABSL_GUARDED_BY(listeners_mutex_) = nullptr;
    jmethodID on_state_changed_mid_ ABSL_GUARDED_BY(listeners_mutex_) = nullptr;
};

/**
 * @class JniLogSink
 * @brief Custom webrtc::LogSink that routes native WebRTC logs to Java/Kotlin LoggingListener via
 * JNI.
 */
class JniLogSink : public webrtc::LogSink {
  public:
    void SetListener(JavaVM* jvm, JNIEnv* env, jobject listener,
                     webrtc::LoggingSeverity min_severity);
    void Disable(JNIEnv* env);

    void OnLogMessage(const std::string& message) override;
    void OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity) override;
    void OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity,
                      const char* tag) override;

  private:
    JNIEnv* GetJniEnv(bool* out_attached);

    JavaVM* jvm_ = nullptr;
    absl::Mutex mutex_;
    jobject listener_ref_ ABSL_GUARDED_BY(mutex_) = nullptr;
    jclass severity_cls_ ABSL_GUARDED_BY(mutex_) = nullptr;
    jmethodID from_value_mid_ ABSL_GUARDED_BY(mutex_) = nullptr;
    jmethodID on_log_message_mid_ ABSL_GUARDED_BY(mutex_) = nullptr;
};

}  // namespace goldfish::videobridge::java

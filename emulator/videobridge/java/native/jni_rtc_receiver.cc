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

#include "jni_rtc_receiver.h"

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/jsep.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "emulator_controller.pb.h"
#include "goldfish/videobridge/codec_factories.h"
#include "modules/audio_device/include/fake_audio_device.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/ref_counted_object.h"

namespace goldfish::videobridge::java {

namespace {

class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
  public:
    void OnSuccess() override {}
    void OnFailure(webrtc::RTCError error) override {
        LOG(ERROR) << "WebRTC SetSessionDescription failed: " << error.message();
    }
};

class CreateSdpObserver : public webrtc::CreateSessionDescriptionObserver {
  public:
    explicit CreateSdpObserver(std::weak_ptr<JniRtcReceiver> receiver)
            : receiver_(std::move(receiver)) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        LOG(INFO) << "CreateSdpObserver::OnSuccess called";
        if (auto receiver = receiver_.lock()) {
            receiver->OnSessionDescriptionCreated(desc);
        } else {
            LOG(ERROR) << "CreateSdpObserver::OnSuccess: receiver.lock() returned null!";
        }
    }

    void OnFailure(webrtc::RTCError error) override {
        LOG(ERROR) << "CreateSdpObserver::OnFailure called: " << error.message();
        if (auto receiver = receiver_.lock()) {
            receiver->OnSessionDescriptionFailure(error);
        }
    }

  private:
    std::weak_ptr<JniRtcReceiver> receiver_;
};

}  // namespace

JniRtcReceiver::JniRtcReceiver(JavaVM* jvm, PixelFormat format)
        : jvm_(jvm), target_format_(format) {
    pool_ = std::make_shared<FrameBufferPool>();
    video_sink_ = std::make_shared<JniVideoSink>(jvm_, target_format_, pool_);
}

JniRtcReceiver::~JniRtcReceiver() {
    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }
    peer_connection_factory_ = nullptr;

    if (signaling_thread_) signaling_thread_->Stop();
    if (worker_thread_) worker_thread_->Stop();
    if (network_thread_) network_thread_->Stop();

    if (jsep_listener_ref_) {
        bool attached = false;
        JNIEnv* env = GetJniEnv(&attached);
        if (env) env->DeleteGlobalRef(jsep_listener_ref_);
        if (attached && jvm_) jvm_->DetachCurrentThread();
    }
}

bool JniRtcReceiver::Initialize() {
    network_thread_ = webrtc::Thread::CreateWithSocketServer();
    network_thread_->Start();
    worker_thread_ = webrtc::Thread::Create();
    worker_thread_->Start();
    signaling_thread_ = webrtc::Thread::Create();
    signaling_thread_->Start();

    auto fake_adm = webrtc::scoped_refptr<webrtc::AudioDeviceModule>(
            new webrtc::RefCountedObject<webrtc::FakeAudioDeviceModule>());

    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
            network_thread_.get(), worker_thread_.get(), signaling_thread_.get(), fake_adm,
            webrtc::CreateBuiltinAudioEncoderFactory(), webrtc::CreateBuiltinAudioDecoderFactory(),
            goldfish::videobridge::CreatePlatformVideoEncoderFactory(),
            goldfish::videobridge::CreatePlatformVideoDecoderFactory(), nullptr /* audio_mixer */,
            nullptr /* audio_processing */);

    if (!peer_connection_factory_) {
        LOG(ERROR) << "Failed to create WebRTC PeerConnectionFactory";
        return false;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

    webrtc::PeerConnectionDependencies dependencies(this);
    auto result =
            peer_connection_factory_->CreatePeerConnectionOrError(config, std::move(dependencies));
    if (!result.ok()) {
        LOG(ERROR) << "Failed to create PeerConnection: " << result.error().message();
        return false;
    }

    peer_connection_ = result.MoveValue();

    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
    peer_connection_->AddTransceiver(webrtc::MediaType::VIDEO, init);

    return true;
}

JNIEnv* JniRtcReceiver::GetJniEnv(bool* out_attached) {
    *out_attached = false;
    if (!jvm_) return nullptr;

    JNIEnv* env = nullptr;
    jint res = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (res == JNI_EDETACHED) {
#if defined(__ANDROID__)
        if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            *out_attached = true;
        }
#else
        if (jvm_->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
            *out_attached = true;
        }
#endif
    }
    return env;
}

void JniRtcReceiver::SetVideoFrameListener(JNIEnv* env, jobject listener) {
    video_sink_->SetListener(env, listener);
}

void JniRtcReceiver::SetJsepMessageListener(JNIEnv* env, jobject listener) {
    absl::MutexLock lock(&listeners_mutex_);
    if (jsep_listener_ref_) {
        env->DeleteGlobalRef(jsep_listener_ref_);
        jsep_listener_ref_ = nullptr;
    }
    if (listener) {
        jsep_listener_ref_ = env->NewGlobalRef(listener);
        jclass cls = env->FindClass("com/android/emulator/webrtc/JsepMessageListener");
        on_outbound_jsep_mid_ =
                env->GetMethodID(cls, "onOutboundJsepMessage", "(Ljava/lang/String;)V");
    }
}

void JniRtcReceiver::SetConnectionStateListener(JNIEnv* env, jobject listener) {
    absl::MutexLock lock(&listeners_mutex_);
    if (state_listener_ref_) {
        env->DeleteGlobalRef(state_listener_ref_);
        state_listener_ref_ = nullptr;
    }
    if (listener) {
        state_listener_ref_ = env->NewGlobalRef(listener);
        jclass cls = env->FindClass("com/android/emulator/webrtc/ConnectionStateListener");
        on_state_changed_mid_ =
                env->GetMethodID(cls, "onConnectionStateChanged",
                                 "(Lcom/android/emulator/webrtc/ConnectionState;)V");
    }
}

void JniRtcReceiver::DispatchOutboundJsep(const nlohmann::json& json_msg) {
    LOG(INFO) << "DispatchOutboundJsep invoked: " << json_msg.dump();
    bool attached = false;
    JNIEnv* env = GetJniEnv(&attached);
    if (!env) {
        LOG(ERROR) << "DispatchOutboundJsep: GetJniEnv returned null";
        return;
    }

    jobject ref = nullptr;
    jmethodID mid = nullptr;
    {
        absl::MutexLock lock(&listeners_mutex_);
        ref = jsep_listener_ref_;
        mid = on_outbound_jsep_mid_;
    }

    if (ref && mid) {
        LOG(INFO) << "DispatchOutboundJsep: calling Java listener";
        std::string json_str = json_msg.dump();
        jstring jjson = env->NewStringUTF(json_str.c_str());
        env->CallVoidMethod(ref, mid, jjson);
        env->DeleteLocalRef(jjson);
    } else {
        LOG(ERROR) << "DispatchOutboundJsep: listener ref or mid is null (ref=" << ref
                   << ", mid=" << mid << ")";
    }

    if (attached && jvm_) jvm_->DetachCurrentThread();
}

void JniRtcReceiver::CreateOffer() {
    LOG(INFO) << "CreateOffer called on JniRtcReceiver";
    if (!peer_connection_) {
        LOG(ERROR) << "CreateOffer: peer_connection_ is null!";
        return;
    }

    auto channel_or_error = peer_connection_->CreateDataChannelOrError("input", nullptr);
    if (channel_or_error.ok()) {
        input_data_channel_ = channel_or_error.MoveValue();
        LOG(INFO) << "Created 'input' data channel on peer_connection_ in CreateOffer";
    } else {
        LOG(ERROR) << "Failed to create 'input' data channel: "
                   << channel_or_error.error().message();
    }

    auto observer = webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver>(
            new webrtc::RefCountedObject<CreateSdpObserver>(weak_from_this()));

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video =
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
    options.offer_to_receive_audio = 0;

    peer_connection_->CreateOffer(observer.get(), options);
}

void JniRtcReceiver::AcceptJsepMessage(JNIEnv* env, const std::string& jsep_json) {
    auto json = nlohmann::json::parse(jsep_json, nullptr, false);
    if (json.is_discarded()) {
        LOG(ERROR) << "Failed to parse incoming JSEP JSON";
        return;
    }

    if (json.contains("sdp")) {
        std::string type_str = json.value("type", "");
        std::string sdp = json.value("sdp", "");

        auto type_opt = webrtc::SdpTypeFromString(type_str);
        if (!type_opt) return;

        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::SessionDescriptionInterface> session_desc =
                webrtc::CreateSessionDescription(*type_opt, sdp, &error);

        if (!session_desc) {
            LOG(ERROR) << "Failed to parse SDP: " << error.description;
            return;
        }

        auto dummy_observer = webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver>(
                new webrtc::RefCountedObject<DummySetSessionDescriptionObserver>());

        peer_connection_->SetRemoteDescription(dummy_observer.get(), session_desc.release());

        if (type_str == "offer") {
            auto create_observer = webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver>(
                    new webrtc::RefCountedObject<CreateSdpObserver>(weak_from_this()));

            peer_connection_->CreateAnswer(
                    create_observer.get(),
                    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
        }
    } else if (json.contains("candidate")) {
        auto candidate_json = json["candidate"];
        std::string sdp_mid = candidate_json.value("sdpMid", "");
        int sdp_mline_index = candidate_json.value("sdpMLineIndex", 0);
        std::string candidate_str = candidate_json.value("candidate", "");

        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
                webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate_str, &error));

        if (candidate) {
            peer_connection_->AddIceCandidate(candidate.get());
        }
    }
}

void JniRtcReceiver::SendTouchEvent(int id, int x, int y, int action_enum) {
    if (!input_data_channel_ ||
        input_data_channel_->state() != webrtc::DataChannelInterface::kOpen) {
        LOG(WARNING) << "SendTouchEvent dropped: input_data_channel_ is "
                     << (input_data_channel_ ? std::to_string(input_data_channel_->state())
                                             : "null");
        return;
    }

    android::emulation::control::InputEvent input_event;
    auto* touch_event = input_event.mutable_touch_event();
    touch_event->set_display(0);
    auto* touch = touch_event->add_touches();
    touch->set_x(x);
    touch->set_y(y);
    touch->set_identifier(id);
    touch->set_pressure((action_enum == 2 || action_enum == 3) ? 0 : 1);
    touch->set_touch_major(10);
    touch->set_touch_minor(10);

    std::string serialized;
    if (!input_event.SerializeToString(&serialized)) {
        LOG(ERROR) << "Failed to serialize InputEvent touch_event";
        return;
    }

    webrtc::DataBuffer buffer(webrtc::CopyOnWriteBuffer(serialized.data(), serialized.size()),
                              /*binary=*/true);
    input_data_channel_->Send(buffer);
}

void JniRtcReceiver::SendMouseEvent(int x, int y, int button_enum, int action_enum) {
    if (!input_data_channel_ ||
        input_data_channel_->state() != webrtc::DataChannelInterface::kOpen) {
        LOG(WARNING) << "SendMouseEvent dropped: input_data_channel_ is "
                     << (input_data_channel_ ? std::to_string(input_data_channel_->state())
                                             : "null");
        return;
    }

    android::emulation::control::InputEvent input_event;
    auto* mouse_event = input_event.mutable_mouse_event();
    mouse_event->set_display(0);
    mouse_event->set_x(x);
    mouse_event->set_y(y);
    mouse_event->set_buttons(button_enum);

    std::string serialized;
    if (!input_event.SerializeToString(&serialized)) {
        LOG(ERROR) << "Failed to serialize InputEvent mouse_event";
        return;
    }

    webrtc::DataBuffer buffer(webrtc::CopyOnWriteBuffer(serialized.data(), serialized.size()),
                              /*binary=*/true);
    input_data_channel_->Send(buffer);
}

void JniRtcReceiver::SendKeyEvent(const std::string& key, bool down) {
    if (!input_data_channel_ ||
        input_data_channel_->state() != webrtc::DataChannelInterface::kOpen) {
        LOG(WARNING) << "SendKeyEvent dropped: input_data_channel_ is "
                     << (input_data_channel_ ? std::to_string(input_data_channel_->state())
                                             : "null");
        return;
    }

    android::emulation::control::InputEvent input_event;
    auto* key_event = input_event.mutable_key_event();
    key_event->set_key(key);
    key_event->set_eventtype(down ? android::emulation::control::KeyboardEvent::keydown
                                  : android::emulation::control::KeyboardEvent::keyup);

    std::string serialized;
    if (!input_event.SerializeToString(&serialized)) {
        LOG(ERROR) << "Failed to serialize InputEvent key_event";
        return;
    }

    webrtc::DataBuffer buffer(webrtc::CopyOnWriteBuffer(serialized.data(), serialized.size()),
                              /*binary=*/true);
    input_data_channel_->Send(buffer);
}

void JniRtcReceiver::OnSessionDescriptionCreated(webrtc::SessionDescriptionInterface* desc) {
    auto dummy_observer = webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver>(
            new webrtc::RefCountedObject<DummySetSessionDescriptionObserver>());

    peer_connection_->SetLocalDescription(dummy_observer.get(), desc);

    std::string sdp;
    desc->ToString(&sdp);
    nlohmann::json json_answer = {{"sdp", sdp}, {"type", webrtc::SdpTypeToString(desc->GetType())}};
    DispatchOutboundJsep(json_answer);
}

void JniRtcReceiver::OnSessionDescriptionFailure(webrtc::RTCError error) {
    LOG(ERROR) << "WebRTC CreateSessionDescription failed: " << error.message();
}

void JniRtcReceiver::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string candidate_str;
    candidate->ToString(&candidate_str);

    nlohmann::json json_candidate = {{"candidate",
                                      {{"candidate", candidate_str},
                                       {"sdpMid", candidate->sdp_mid()},
                                       {"sdpMLineIndex", candidate->sdp_mline_index()}}}};

    DispatchOutboundJsep(json_candidate);
}

void JniRtcReceiver::OnDataChannel(
        webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
    LOG(INFO) << "JniRtcReceiver: OnDataChannel received data channel '" << data_channel->label()
              << "', state=" << data_channel->state();
    if (data_channel->label() == "input") {
        input_data_channel_ = data_channel;
    }
}

void JniRtcReceiver::OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    auto track = transceiver->receiver()->track();
    if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
        auto video_track = static_cast<webrtc::VideoTrackInterface*>(track.get());
        video_track->AddOrUpdateSink(video_sink_.get(), webrtc::VideoSinkWants());
    }
}

void JniRtcReceiver::OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    bool attached = false;
    JNIEnv* env = GetJniEnv(&attached);
    if (!env) return;

    jobject listener_ref = nullptr;
    jmethodID mid = nullptr;

    {
        absl::MutexLock lock(&listeners_mutex_);
        if (!state_listener_ref_ || !on_state_changed_mid_) {
            if (attached && jvm_) jvm_->DetachCurrentThread();
            return;
        }
        listener_ref = state_listener_ref_;
        mid = on_state_changed_mid_;
    }

    jclass state_cls = env->FindClass("com/android/emulator/webrtc/ConnectionState");
    jfieldID field_id = nullptr;

    switch (new_state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
        field_id = env->GetStaticFieldID(state_cls, "NEW",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
        field_id = env->GetStaticFieldID(state_cls, "CONNECTING",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        field_id = env->GetStaticFieldID(state_cls, "CONNECTED",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
        field_id = env->GetStaticFieldID(state_cls, "DISCONNECTED",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
        field_id = env->GetStaticFieldID(state_cls, "FAILED",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        field_id = env->GetStaticFieldID(state_cls, "CLOSED",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    default:
        field_id = env->GetStaticFieldID(state_cls, "CONNECTING",
                                         "Lcom/android/emulator/webrtc/ConnectionState;");
        break;
    }

    if (field_id) {
        jobject enum_obj = env->GetStaticObjectField(state_cls, field_id);
        if (enum_obj) {
            env->CallVoidMethod(listener_ref, mid, enum_obj);
            env->DeleteLocalRef(enum_obj);
        }
    }
    if (state_cls) env->DeleteLocalRef(state_cls);

    if (attached && jvm_) {
        jvm_->DetachCurrentThread();
    }
}

void JniLogSink::SetListener(JavaVM* jvm, JNIEnv* env, jobject listener,
                             webrtc::LoggingSeverity min_severity) {
    // Remove existing stream registration without holding JniLogSink mutex
    webrtc::LogMessage::RemoveLogToStream(this);

    jobject new_listener_ref = nullptr;
    jclass new_severity_cls = nullptr;
    jmethodID new_from_value_mid = nullptr;
    jmethodID new_on_log_message_mid = nullptr;

    if (listener && min_severity != webrtc::LS_NONE) {
        new_listener_ref = env->NewGlobalRef(listener);
        jclass listener_cls = env->GetObjectClass(listener);
        jclass severity_cls = env->FindClass("com/android/emulator/webrtc/LoggingSeverity");
        new_severity_cls = reinterpret_cast<jclass>(env->NewGlobalRef(severity_cls));
        new_from_value_mid = env->GetStaticMethodID(
                new_severity_cls, "fromValue", "(I)Lcom/android/emulator/webrtc/LoggingSeverity;");
        new_on_log_message_mid = env->GetMethodID(
                listener_cls, "onLogMessage",
                "(Lcom/android/emulator/webrtc/LoggingSeverity;Ljava/lang/String;)V");
    }

    jobject old_listener_ref = nullptr;
    jclass old_severity_cls = nullptr;

    {
        absl::MutexLock lock(&mutex_);
        old_listener_ref = listener_ref_;
        old_severity_cls = severity_cls_;

        jvm_ = jvm;
        listener_ref_ = new_listener_ref;
        severity_cls_ = new_severity_cls;
        from_value_mid_ = new_from_value_mid;
        on_log_message_mid_ = new_on_log_message_mid;
    }

    if (old_listener_ref) env->DeleteGlobalRef(old_listener_ref);
    if (old_severity_cls) env->DeleteGlobalRef(old_severity_cls);

    if (new_listener_ref && min_severity != webrtc::LS_NONE) {
        webrtc::LogMessage::AddLogToStream(this, min_severity);
    }
}

JNIEnv* JniLogSink::GetJniEnv(bool* out_attached) {
    *out_attached = false;
    if (!jvm_) return nullptr;

    JNIEnv* env = nullptr;
    jint res = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (jvm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
            *out_attached = true;
        } else {
            return nullptr;
        }
    }
    return env;
}

void JniLogSink::OnLogMessage(const std::string& message) {
    OnLogMessage(message, webrtc::LS_INFO);
}

void JniLogSink::OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity,
                              const char* tag) {
    OnLogMessage(message, severity);
}

void JniLogSink::OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity) {
    static thread_local bool in_log_callback = false;
    if (in_log_callback) return;

    struct ReentrancyGuard {
        ReentrancyGuard() { in_log_callback = true; }
        ~ReentrancyGuard() { in_log_callback = false; }
    } guard;

    JavaVM* jvm = nullptr;
    jobject listener_ref = nullptr;
    jclass severity_cls = nullptr;
    jmethodID from_val_mid = nullptr;
    jmethodID on_log_mid = nullptr;

    {
        absl::MutexLock lock(&mutex_);
        if (!listener_ref_ || !on_log_message_mid_ || !severity_cls_ || !from_value_mid_) {
            return;
        }
        jvm = jvm_;
        listener_ref = listener_ref_;
        severity_cls = severity_cls_;
        from_val_mid = from_value_mid_;
        on_log_mid = on_log_message_mid_;
    }

    if (!jvm || !listener_ref) return;

    bool attached = false;
    JNIEnv* env = nullptr;
    jint res = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    }
    if (!env) return;

    std::string_view sv = message;
    if (!sv.empty() && sv.back() == '\n') {
        sv.remove_suffix(1);
    }

    jobject severity_obj =
            env->CallStaticObjectMethod(severity_cls, from_val_mid, static_cast<jint>(severity));
    jstring msg_str = env->NewStringUTF(std::string(sv).c_str());

    if (severity_obj && msg_str) {
        env->CallVoidMethod(listener_ref, on_log_mid, severity_obj, msg_str);
    }

    if (severity_obj) env->DeleteLocalRef(severity_obj);
    if (msg_str) env->DeleteLocalRef(msg_str);

    if (attached && jvm) {
        jvm->DetachCurrentThread();
    }
}

static JniLogSink g_jni_log_sink;

}  // namespace goldfish::videobridge::java

using goldfish::videobridge::java::JniRtcReceiver;
using goldfish::videobridge::java::PixelFormat;

extern "C" {

JNIEXPORT jlong JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeCreate(
        JNIEnv* env, jclass clazz, jint format_ordinal) {
    JavaVM* jvm = nullptr;
    env->GetJavaVM(&jvm);

    auto receiver = std::make_shared<JniRtcReceiver>(jvm, static_cast<PixelFormat>(format_ordinal));
    if (!receiver->Initialize()) {
        return 0;
    }
    // Retain shared_ptr instance on native heap
    auto raw_ptr = new std::shared_ptr<JniRtcReceiver>(receiver);
    return reinterpret_cast<jlong>(raw_ptr);
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSetVideoFrameListener(
        JNIEnv* env, jclass clazz, jlong handle, jobject listener) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) (*receiver_ptr)->SetVideoFrameListener(env, listener);
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSetJsepMessageListener(
        JNIEnv* env, jclass clazz, jlong handle, jobject listener) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) (*receiver_ptr)->SetJsepMessageListener(env, listener);
}

JNIEXPORT void JNICALL
Java_com_android_emulator_webrtc_WebRtcClient_nativeSetConnectionStateListener(JNIEnv* env,
                                                                               jclass clazz,
                                                                               jlong handle,
                                                                               jobject listener) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) (*receiver_ptr)->SetConnectionStateListener(env, listener);
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeCreateOffer(
        JNIEnv* env, jclass clazz, jlong handle) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) (*receiver_ptr)->CreateOffer();
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeAcceptJsepMessage(
        JNIEnv* env, jclass clazz, jlong handle, jstring jsep_json) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr && jsep_json) {
        const char* str = env->GetStringUTFChars(jsep_json, nullptr);
        (*receiver_ptr)->AcceptJsepMessage(env, str);
        env->ReleaseStringUTFChars(jsep_json, str);
    }
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSendTouchEvent(
        JNIEnv* env, jclass clazz, jlong handle, jint id, jint x, jint y, jint action_enum) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) {
        (*receiver_ptr)->SendTouchEvent(id, x, y, action_enum);
    }
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSendMouseEvent(
        JNIEnv* env, jclass clazz, jlong handle, jint x, jint y, jint button_enum,
        jint action_enum) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr) {
        (*receiver_ptr)->SendMouseEvent(x, y, button_enum, action_enum);
    }
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSendKeyEvent(
        JNIEnv* env, jclass clazz, jlong handle, jstring key, jboolean down) {
    auto receiver_ptr = reinterpret_cast<std::shared_ptr<JniRtcReceiver>*>(handle);
    if (receiver_ptr && *receiver_ptr && key) {
        const char* str = env->GetStringUTFChars(key, nullptr);
        (*receiver_ptr)->SendKeyEvent(str, down);
        env->ReleaseStringUTFChars(key, str);
    }
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeSetLoggingListener(
        JNIEnv* env, jclass clazz, jint min_severity, jobject listener) {
    JavaVM* jvm = nullptr;
    env->GetJavaVM(&jvm);
    goldfish::videobridge::java::g_jni_log_sink.SetListener(
            jvm, env, listener, static_cast<webrtc::LoggingSeverity>(min_severity));
}

JNIEXPORT void JNICALL Java_com_android_emulator_webrtc_WebRtcClient_nativeDestroy(JNIEnv* env,
                                                                                   jclass clazz,
                                                                                   jlong handle) {
    auto receiver_ptr =
            reinterpret_cast<std::shared_ptr<goldfish::videobridge::java::JniRtcReceiver>*>(handle);
    delete receiver_ptr;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    webrtc::LogMessage::LogToDebug(webrtc::LS_NONE);
    webrtc::LogMessage::SetLogToStderr(false);
    return JNI_VERSION_1_6;
}

}  // extern "C"

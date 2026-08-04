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

#include "jni_video_sink.h"

#include <utility>

#include "absl/log/log.h"

#include "libyuv/convert.h"
#include "libyuv/convert_from.h"

namespace goldfish::videobridge::java {

JniVideoSink::JniVideoSink(JavaVM* jvm, PixelFormat format, std::shared_ptr<FrameBufferPool> pool)
        : jvm_(jvm), target_format_(format), pool_(std::move(pool)) {}

JniVideoSink::~JniVideoSink() {
    if (listener_global_ref_ != nullptr) {
        bool attached = false;
        JNIEnv* env = GetJniEnv(&attached);
        if (env) {
            env->DeleteGlobalRef(listener_global_ref_);
            if (video_frame_cls_) {
                env->DeleteGlobalRef(video_frame_cls_);
            }
        }
        if (attached && jvm_) {
            jvm_->DetachCurrentThread();
        }
    }
}

JNIEnv* JniVideoSink::GetJniEnv(bool* out_attached) {
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

void JniVideoSink::SetListener(JNIEnv* env, jobject listener) {
    absl::MutexLock lock(&listener_mutex_);
    if (listener_global_ref_ != nullptr) {
        env->DeleteGlobalRef(listener_global_ref_);
        listener_global_ref_ = nullptr;
    }
    if (video_frame_cls_ != nullptr) {
        env->DeleteGlobalRef(video_frame_cls_);
        video_frame_cls_ = nullptr;
    }

    if (listener != nullptr) {
        listener_global_ref_ = env->NewGlobalRef(listener);
        jclass listener_cls = env->FindClass("com/android/emulator/webrtc/VideoFrameListener");
        on_video_frame_mid_ = env->GetMethodID(listener_cls, "onVideoFrame",
                                               "(Lcom/android/emulator/webrtc/VideoFrame;)V");

        jclass vf_cls = env->FindClass("com/android/emulator/webrtc/VideoFrame");
        if (vf_cls) {
            video_frame_cls_ = reinterpret_cast<jclass>(env->NewGlobalRef(vf_cls));
            video_frame_ctor_ =
                    env->GetMethodID(video_frame_cls_, "<init>",
                                     "(IILcom/android/emulator/webrtc/PixelFormat;JLjava/nio/"
                                     "ByteBuffer;Ljava/lang/Runnable;)V");
        }
    }
}

void JniVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
    bool attached = false;
    JNIEnv* env = GetJniEnv(&attached);
    if (!env) return;

    jobject listener_ref = nullptr;
    jclass vf_cls = nullptr;
    jmethodID ctor = nullptr;
    jmethodID on_frame_mid = nullptr;

    {
        absl::MutexLock lock(&listener_mutex_);
        if (!listener_global_ref_ || !video_frame_cls_) {
            if (attached && jvm_) jvm_->DetachCurrentThread();
            return;
        }
        listener_ref = listener_global_ref_;
        vf_cls = video_frame_cls_;
        ctor = video_frame_ctor_;
        on_frame_mid = on_video_frame_mid_;
    }

    int width = frame.width();
    int height = frame.height();
    size_t required_capacity = width * height * 4;

    auto native_buffer = pool_->Acquire(required_capacity);

    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = frame.video_frame_buffer()->ToI420();

    if (target_format_ == PixelFormat::kRgba8888) {
        libyuv::I420ToRGBA(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(),
                           i420->DataV(), i420->StrideV(), native_buffer->data(), width * 4, width,
                           height);
    } else if (target_format_ == PixelFormat::kBgra8888) {
        libyuv::I420ToBGRA(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(),
                           i420->DataV(), i420->StrideV(), native_buffer->data(), width * 4, width,
                           height);
    }

    jobject direct_buffer = env->NewDirectByteBuffer(native_buffer->data(), required_capacity);

    // Create PixelFormat enum Java object instance (RGBA8888 ordinal = 0, BGRA8888 ordinal = 1)
    jclass pixel_format_cls = env->FindClass("com/android/emulator/webrtc/PixelFormat");
    jfieldID enum_field = nullptr;
    if (target_format_ == PixelFormat::kRgba8888) {
        enum_field = env->GetStaticFieldID(pixel_format_cls, "RGBA8888",
                                           "Lcom/android/emulator/webrtc/PixelFormat;");
    } else {
        enum_field = env->GetStaticFieldID(pixel_format_cls, "BGRA8888",
                                           "Lcom/android/emulator/webrtc/PixelFormat;");
    }
    jobject enum_obj = env->GetStaticObjectField(pixel_format_cls, enum_field);

    // Create release callback Runnable by invoking VideoFrame.createReleaseCallback(buffer_id,
    // pool_ptr)
    jmethodID create_cb_mid =
            env->GetStaticMethodID(vf_cls, "createReleaseCallback", "(JJ)Ljava/lang/Runnable;");
    jobject release_cb = nullptr;
    if (create_cb_mid) {
        release_cb = env->CallStaticObjectMethod(vf_cls, create_cb_mid,
                                                 static_cast<jlong>(native_buffer->id()),
                                                 reinterpret_cast<jlong>(pool_.get()));
    }

    // Construct Java VideoFrame object with release_cb
    jobject java_frame = env->NewObject(vf_cls, ctor, width, height, enum_obj,
                                        static_cast<jlong>(frame.timestamp_us() * 1000),
                                        direct_buffer, release_cb);

    // Invoke Kotlin/Java callback
    env->CallVoidMethod(listener_ref, on_frame_mid, java_frame);

    // Clean up JNI local references
    if (release_cb) env->DeleteLocalRef(release_cb);
    if (java_frame) env->DeleteLocalRef(java_frame);
    if (enum_obj) env->DeleteLocalRef(enum_obj);
    if (pixel_format_cls) env->DeleteLocalRef(pixel_format_cls);
    if (direct_buffer) env->DeleteLocalRef(direct_buffer);

    if (attached && jvm_) {
        jvm_->DetachCurrentThread();
    }
}

}  // namespace goldfish::videobridge::java

extern "C" JNIEXPORT void JNICALL
Java_com_android_emulator_webrtc_VideoFrame_nativeReleaseFrameBuffer(JNIEnv* env, jclass clazz,
                                                                     jlong buffer_id,
                                                                     jlong pool_ptr) {
    if (pool_ptr != 0 && buffer_id != 0) {
        auto* pool = reinterpret_cast<goldfish::videobridge::java::FrameBufferPool*>(pool_ptr);
        pool->Release(static_cast<uint64_t>(buffer_id));
    }
}

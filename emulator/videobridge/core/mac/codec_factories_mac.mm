#include "goldfish/videobridge/codec_factories.h"

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <mutex>
#import "sdk/objc/components/video_codec/RTCVideoDecoderFactoryH264.h"
#import "sdk/objc/components/video_codec/RTCVideoEncoderFactoryH264.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "rtc_base/checks.h"

namespace goldfish::videobridge {

namespace {

/**
 * @class DeferredReleaseVideoEncoder
 * @brief Thread-safe, ref-counted lifetime safeguard for WebRTC's hardware VideoEncoder.
 *
 * @details
 * Hardware video encoding via Apple's VideoToolbox occurs asynchronously on a background
 * GPU queue. When WebRTC discards or switches the active encoder, it immediately destroys
 * the C++ @c VideoEncoder wrapper, which in turn deallocates the underlying Objective-C
 * @c RTCVideoEncoderH264 instance.
 *
 * If GPU frames are still in-flight, the VideoToolbox callback queue will eventually fire
 * and attempt to invoke @c frameWasEncoded: on the deallocated encoder instance, causing a
 * Use-After-Free crash (@c EXC_BAD_ACCESS).
 *
 * @c DeferredReleaseVideoEncoder solves this by separating the encoder into a ref-counted
 * implementation. It tracks the number of in-flight frames (@c pending_frames_). The
 * callback wrapper (@c SafeCallbackWrapper) holds a @c std::shared_ptr to this class,
 * keeping it alive on the heap. When the outer proxy is destroyed, the actual @c Release()
 * and deletion of the underlying encoder are deferred until the last in-flight frame completes,
 * ensuring 100% safe, zero-delay, and deterministic cleanup.
 */
class DeferredReleaseVideoEncoder : public webrtc::VideoEncoder,
                                    public std::enable_shared_from_this<DeferredReleaseVideoEncoder> {
 public:
  explicit DeferredReleaseVideoEncoder(std::unique_ptr<webrtc::VideoEncoder> delegate)
      : delegate_(std::move(delegate)), pending_frames_(0), released_(false) {}

  void Close() {
    {
      absl::MutexLock lock(&mutex_);
      released_ = true;
    }
    MaybeDelete();
  }

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings, int32_t number_of_cores,
                     size_t max_payload_size) override {
    absl::MutexLock lock(&mutex_);
    RTC_DCHECK(!released_) << "InitEncode() called after DeferredReleaseVideoEncoder was closed.";
    return delegate_->InitEncode(codec_settings, number_of_cores, max_payload_size);
  }

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override {
    absl::MutexLock lock(&mutex_);
    RTC_DCHECK(!released_) << "InitEncode() called after DeferredReleaseVideoEncoder was closed.";
    return delegate_->InitEncode(codec_settings, settings);
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    absl::MutexLock lock(&mutex_);
    RTC_DCHECK(!released_) << "RegisterEncodeCompleteCallback() called after DeferredReleaseVideoEncoder was closed.";

    if (callback) {
      wrapped_callback_ = std::make_unique<FrameTrackingCallbackWrapper>(callback, shared_from_this());
      return delegate_->RegisterEncodeCompleteCallback(wrapped_callback_.get());
    } else {
      wrapped_callback_.reset();
      return delegate_->RegisterEncodeCompleteCallback(nullptr);
    }
  }

  int32_t Release() override {
    return 0; // WEBRTC_VIDEO_CODEC_OK
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    webrtc::VideoEncoder* encoder = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      RTC_DCHECK(!released_) << "Encode() called after DeferredReleaseVideoEncoder was closed.";
      pending_frames_++;
      encoder = delegate_.get();
    }

    // Call Encode on the raw pointer outside the lock. This is safe because
    // pending_frames_ > 0 prevents delegate_ from being deallocated.
    int32_t result = encoder->Encode(frame, frame_types);
    if (result != 0) {
      DecrementPendingFrames();
    }
    return result;
  }

  void SetRates(const RateControlParameters& parameters) override {
    delegate_->SetRates(parameters);
  }

  void OnPacketLossRateUpdate(float packet_loss_rate) override {
    delegate_->OnPacketLossRateUpdate(packet_loss_rate);
  }

  void OnRttUpdate(int64_t rtt_ms) override {
    delegate_->OnRttUpdate(rtt_ms);
  }

  void OnLossNotification(const LossNotification& loss_notification) override {
    delegate_->OnLossNotification(loss_notification);
  }

  void SetFecControllerOverride(webrtc::FecControllerOverride* fec_controller_override) override {
    delegate_->SetFecControllerOverride(fec_controller_override);
  }

  EncoderInfo GetEncoderInfo() const override {
    absl::MutexLock lock(&mutex_);
    if (released_) return EncoderInfo();
    return delegate_->GetEncoderInfo();
  }

 private:
  class FrameTrackingCallbackWrapper : public webrtc::EncodedImageCallback {
   public:
    FrameTrackingCallbackWrapper(webrtc::EncodedImageCallback* delegate,
                                 std::shared_ptr<DeferredReleaseVideoEncoder> parent)
        : delegate_(delegate), parent_(parent) {}

    Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                          const webrtc::CodecSpecificInfo* codec_specific_info) override {
      Result result = delegate_->OnEncodedImage(encoded_image, codec_specific_info);
      parent_->DecrementPendingFrames();
      return result;
    }

    void OnFrameDropped(uint32_t rtp_timestamp,
                        int spatial_id,
                        bool is_end_of_temporal_unit) override {
      delegate_->OnFrameDropped(rtp_timestamp, spatial_id, is_end_of_temporal_unit);
      parent_->DecrementPendingFrames();
    }

   private:
    webrtc::EncodedImageCallback* delegate_;
    std::shared_ptr<DeferredReleaseVideoEncoder> parent_;
  };

  void DecrementPendingFrames() {
    {
      absl::MutexLock lock(&mutex_);
      pending_frames_--;
    }
    MaybeDelete();
  }

  void MaybeDelete() {
    std::unique_ptr<webrtc::VideoEncoder> to_delete = nullptr;
    std::unique_ptr<webrtc::EncodedImageCallback> callback_to_delete = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      if (released_ && pending_frames_ == 0) {
        to_delete = std::move(delegate_);
        callback_to_delete = std::move(wrapped_callback_);
      }
    }
    if (to_delete) {
      to_delete->Release();
    }
  }

  std::unique_ptr<webrtc::VideoEncoder> delegate_;
  int pending_frames_ ABSL_GUARDED_BY(mutex_);
  bool released_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  std::unique_ptr<webrtc::EncodedImageCallback> wrapped_callback_ ABSL_GUARDED_BY(mutex_);
};

/**
 * @class DeferredReleaseVideoEncoderProxy
 * @brief A thin unique_ptr proxy returned to WebRTC.
 *
 * @details
 * Since WebRTC APIs expect to own the VideoEncoder via std::unique_ptr, we return this
 * proxy. It delegates all operations to the underlying ref-counted @c DeferredReleaseVideoEncoder.
 * When WebRTC destroys this proxy, it triggers @c Close() on the inner encoder, which
 * safely defers cleanup until all in-flight GPU frames complete.
 */
class DeferredReleaseVideoEncoderProxy : public webrtc::VideoEncoder {
 public:
  explicit DeferredReleaseVideoEncoderProxy(std::unique_ptr<webrtc::VideoEncoder> delegate)
      : inner_(std::make_shared<DeferredReleaseVideoEncoder>(std::move(delegate))) {}

  ~DeferredReleaseVideoEncoderProxy() override {
    inner_->Close();
  }

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings, int32_t number_of_cores,
                     size_t max_payload_size) override {
    return inner_->InitEncode(codec_settings, number_of_cores, max_payload_size);
  }

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override {
    return inner_->InitEncode(codec_settings, settings);
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    return inner_->RegisterEncodeCompleteCallback(callback);
  }

  int32_t Release() override {
    return inner_->Release();
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    return inner_->Encode(frame, frame_types);
  }

  void SetRates(const RateControlParameters& parameters) override {
    inner_->SetRates(parameters);
  }

  void OnPacketLossRateUpdate(float packet_loss_rate) override {
    inner_->OnPacketLossRateUpdate(packet_loss_rate);
  }

  void OnRttUpdate(int64_t rtt_ms) override {
    inner_->OnRttUpdate(rtt_ms);
  }

  void OnLossNotification(const LossNotification& loss_notification) override {
    inner_->OnLossNotification(loss_notification);
  }

  void SetFecControllerOverride(webrtc::FecControllerOverride* fec_controller_override) override {
    inner_->SetFecControllerOverride(fec_controller_override);
  }

  EncoderInfo GetEncoderInfo() const override {
    return inner_->GetEncoderInfo();
  }

 private:
  std::shared_ptr<DeferredReleaseVideoEncoder> inner_;
};

/**
 * @class CompositeVideoEncoderFactory
 * @brief A composite factory that merges hardware-accelerated and software video encoder factories.
 *
 * @details
 * This factory is used to prioritize Apple's hardware-accelerated H.264 encoding via VideoToolbox
 * while falling back to WebRTC's software implementations (VP8, VP9, AV1) for other formats.
 *
 * It also performs two critical duties during codec negotiation:
 * 1. **Format Elevation**: It intercepts the supported H.264 formats and forces them to advertise
 *    High Profile and Level 5.2 ("640c34") during SDP negotiation. This ensures WebRTC allows
 *    high-resolution encoding (e.g. 1080x2400 screenshare) instead of capping at the default
 *    Level 4.1.
 * 2. **Lifecycle Safety**: It wraps all created hardware H.264 encoders in a
 *    @c DeferredReleaseVideoEncoderProxy to prevent Use-After-Free crashes during stream renegotiation.
 */
class CompositeVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  CompositeVideoEncoderFactory(std::unique_ptr<webrtc::VideoEncoderFactory> hardware_factory,
                               std::unique_ptr<webrtc::VideoEncoderFactory> software_factory,
                               RTCVideoEncoderFactoryH264* objc_factory)
      : hardware_factory_(std::move(hardware_factory)),
        software_factory_(std::move(software_factory)),
        objc_factory_(objc_factory) {
    (void)objc_factory_;
  }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> formats;
    // Prioritize hardware formats (H.264)
    if (hardware_factory_) {
      auto hw_formats = hardware_factory_->GetSupportedFormats();
      for (auto& format : hw_formats) {
        if (format.name == "H264") {
          auto it = format.parameters.find("profile-level-id");
          if (it != format.parameters.end()) {
            std::string profile_level = it->second;
            if (profile_level.size() == 6) {
              // Force Level 5.2 ("34") to support 1080x2400
              // This is needed because the default Level 4.1 ("2e") is too low
              // and will reject larger android displays.
              profile_level = profile_level.substr(0, 4) + "34";
              format.parameters["profile-level-id"] = profile_level;
            }
          }
        }
        formats.push_back(format);
      }
    }
    // Add software fallbacks (VP8, VP9, AV1)
    if (software_factory_) {
      auto sw_formats = software_factory_->GetSupportedFormats();
      formats.insert(formats.end(), sw_formats.begin(), sw_formats.end());
    }
    return formats;
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment& env,
                                               const webrtc::SdpVideoFormat& format) override {
    // Try creating via hardware first
    if (hardware_factory_) {
      for (const auto& hw_format : hardware_factory_->GetSupportedFormats()) {
        if (hw_format.IsSameCodec(format)) {
          webrtc::SdpVideoFormat elevated_format = format;
          if (elevated_format.name == "H264") {
            // Force High Profile Level 5.2 ("640c34") to ensure hardware compatibility
            // and support for high resolutions like 1080x2400.
            elevated_format.parameters["profile-level-id"] = "640c34";
          }
          auto encoder = hardware_factory_->Create(env, elevated_format);
          if (encoder) {
            return std::make_unique<DeferredReleaseVideoEncoderProxy>(std::move(encoder));
          }
        }
      }
    }
    // Fallback to software
    if (software_factory_) {
      return software_factory_->Create(env, format);
    }
    return nullptr;
  }

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> hardware_factory_;
  std::unique_ptr<webrtc::VideoEncoderFactory> software_factory_;
  RTCVideoEncoderFactoryH264* objc_factory_;
};

/**
 * @class CompositeVideoDecoderFactory
 * @brief A composite factory that merges hardware-accelerated and software video decoder factories.
 *
 * @details
 * This factory is used to prioritize Apple's hardware-accelerated H.264 decoding via VideoToolbox
 * (which uses `VTDecompressionSession` internally) while falling back to WebRTC's software
 * implementations (VP8, VP9, AV1) for other formats.
 */
class CompositeVideoDecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  CompositeVideoDecoderFactory(std::unique_ptr<webrtc::VideoDecoderFactory> hardware_factory,
                               std::unique_ptr<webrtc::VideoDecoderFactory> software_factory,
                               RTCVideoDecoderFactoryH264* objc_factory)
      : hardware_factory_(std::move(hardware_factory)),
        software_factory_(std::move(software_factory)),
        objc_factory_(objc_factory) {
    (void)objc_factory_;
  }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> formats;
    if (hardware_factory_) {
      auto hw_formats = hardware_factory_->GetSupportedFormats();
      for (auto& format : hw_formats) {
        if (format.name == "H264") {
          auto it = format.parameters.find("profile-level-id");
          if (it != format.parameters.end()) {
            std::string profile_level = it->second;
            if (profile_level.size() == 6) {
              profile_level = profile_level.substr(0, 4) + "34";
              format.parameters["profile-level-id"] = profile_level;
            }
          }
        }
        formats.push_back(format);
      }
    }
    if (software_factory_) {
      auto sw_formats = software_factory_->GetSupportedFormats();
      formats.insert(formats.end(), sw_formats.begin(), sw_formats.end());
    }
    return formats;
  }

  std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment& env,
                                               const webrtc::SdpVideoFormat& format) override {
    if (hardware_factory_) {
      for (const auto& hw_format : hardware_factory_->GetSupportedFormats()) {
        if (hw_format.IsSameCodec(format)) {
          return hardware_factory_->Create(env, format);
        }
      }
    }
    if (software_factory_) {
      return software_factory_->Create(env, format);
    }
    return nullptr;
  }

 private:
  std::unique_ptr<webrtc::VideoDecoderFactory> hardware_factory_;
  std::unique_ptr<webrtc::VideoDecoderFactory> software_factory_;
  RTCVideoDecoderFactoryH264* objc_factory_;
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePlatformVideoEncoderFactory() {
  RTCVideoEncoderFactoryH264* objc_encoder_factory = [[RTCVideoEncoderFactoryH264 alloc] init];
  auto hardware_factory = webrtc::ObjCToNativeVideoEncoderFactory(objc_encoder_factory);
  auto software_factory = webrtc::CreateBuiltinVideoEncoderFactory();

  return std::make_unique<CompositeVideoEncoderFactory>(
      std::move(hardware_factory), std::move(software_factory), objc_encoder_factory);
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePlatformVideoDecoderFactory() {
  RTCVideoDecoderFactoryH264* objc_decoder_factory = [[RTCVideoDecoderFactoryH264 alloc] init];
  auto hardware_factory = webrtc::ObjCToNativeVideoDecoderFactory(objc_decoder_factory);
  auto software_factory = webrtc::CreateBuiltinVideoDecoderFactory();

  return std::make_unique<CompositeVideoDecoderFactory>(
      std::move(hardware_factory), std::move(software_factory), objc_decoder_factory);
}

}  // namespace goldfish::videobridge

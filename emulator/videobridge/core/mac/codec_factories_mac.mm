#include "goldfish/videobridge/codec_factories.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#import "components/video_frame_buffer/RTCCVPixelBuffer.h"
#import "sdk/objc/components/video_codec/RTCVideoDecoderFactoryH264.h"
#import "sdk/objc/components/video_codec/RTCVideoEncoderFactoryH264.h"
#include "sdk/objc/native/api/video_decoder_factory.h"
#include "sdk/objc/native/api/video_encoder_factory.h"
#include "sdk/objc/native/api/video_frame_buffer.h"

#include "absl/base/thread_annotations.h"
#include "absl/strings/ascii.h"
#include "absl/synchronization/mutex.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "libyuv/planar_functions.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace goldfish::videobridge {

namespace {

/**
 * @class DeferredReleaseVideoEncoder
 * @brief Thread-safe lifetime safeguard and NV12 fast-path adapter for WebRTC's
 *        macOS VideoToolbox H.264 VideoEncoder.
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
 * @c DeferredReleaseVideoEncoder solves this by:
 * 1. Disconnecting its @c FrameTrackingCallbackWrapper and invoking @c Release()
 *    (which calls @c VTCompressionSessionInvalidate to synchronously drain all
 *    in-flight VideoToolbox callbacks) outside @c mutex_ before deallocating the
 *    underlying encoder or callback wrapper.
 * 2. Converting incoming @c webrtc::VideoFrameBuffer::Type::kNV12 frames directly
 *    into pooled, @c IOSurface-backed @c CVPixelBufferRef instances wrapped in
 *    @c RTCCVPixelBuffer (@c Type::kNative), bypassing WebRTC's default
 *    @c NV12 -> @c I420 -> @c NV12 double conversion in @c ToObjCVideoFrameBuffer.
 */
class DeferredReleaseVideoEncoder : public webrtc::VideoEncoder {
 private:
  class FrameTrackingCallbackWrapper : public webrtc::EncodedImageCallback {
   public:
    explicit FrameTrackingCallbackWrapper(webrtc::EncodedImageCallback* delegate)
        : delegate_(delegate) {}

    void Disconnect() {
      absl::MutexLock lock(&callback_mutex_);
      delegate_ = nullptr;
    }

    Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                          const webrtc::CodecSpecificInfo* codec_specific_info) override {
      absl::MutexLock lock(&callback_mutex_);
      if (!delegate_) {
        return Result(Result::OK);
      }
      return delegate_->OnEncodedImage(encoded_image, codec_specific_info);
    }

    void OnFrameDropped(uint32_t rtp_timestamp,
                        int spatial_id,
                        bool is_end_of_temporal_unit) override {
      absl::MutexLock lock(&callback_mutex_);
      if (!delegate_) {
        return;
      }
      delegate_->OnFrameDropped(rtp_timestamp, spatial_id, is_end_of_temporal_unit);
    }

   private:
    absl::Mutex callback_mutex_;
    webrtc::EncodedImageCallback* delegate_ ABSL_GUARDED_BY(callback_mutex_);
  };

 public:
  explicit DeferredReleaseVideoEncoder(std::unique_ptr<webrtc::VideoEncoder> delegate)
      : delegate_(std::move(delegate)),
        released_(false),
        initialized_(false),
        use_settings_overload_(false),
        number_of_cores_(1),
        max_payload_size_(1200),
        settings_(webrtc::VideoEncoder::Capabilities(false), 1, 1200),
        pixel_buffer_pool_(nullptr),
        pool_width_(0),
        pool_height_(0) {}

  ~DeferredReleaseVideoEncoder() override {
    Close();
  }

  void Close() {
    std::unique_ptr<webrtc::VideoEncoder> to_delete = nullptr;
    std::unique_ptr<FrameTrackingCallbackWrapper> callback_to_delete = nullptr;
    CVPixelBufferPoolRef pool_to_release = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      released_ = true;
      initialized_ = false;
      to_delete = std::move(delegate_);
      callback_to_delete = std::move(wrapped_callback_);
      pool_to_release = pixel_buffer_pool_;
      pixel_buffer_pool_ = nullptr;
      pool_width_ = 0;
      pool_height_ = 0;
    }
    if (pool_to_release) {
      CVPixelBufferPoolRelease(pool_to_release);
    }
    if (callback_to_delete) {
      callback_to_delete->Disconnect();
    }
    if (to_delete) {
      // Release() invokes [RTCVideoEncoderH264 releaseEncoder] ->
      // VTCompressionSessionInvalidate(), which synchronously waits for any
      // in-flight VideoToolbox compressionOutputCallback to finish before
      // callback_to_delete and to_delete are destroyed at the end of Close().
      to_delete->Release();
    }
  }

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings, int32_t number_of_cores,
                     size_t max_payload_size) override {
    return InitEncodeInternal(codec_settings, number_of_cores, max_payload_size, nullptr);
  }

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override {
    return InitEncodeInternal(codec_settings, settings.number_of_cores,
                              settings.max_payload_size, &settings);
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    absl::MutexLock lock(&mutex_);
    if (released_ || !delegate_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    if (callback) {
      if (wrapped_callback_) {
        wrapped_callback_->Disconnect();
      }
      wrapped_callback_ = std::make_unique<FrameTrackingCallbackWrapper>(callback);
      return delegate_->RegisterEncodeCompleteCallback(wrapped_callback_.get());
    } else {
      if (wrapped_callback_) {
        wrapped_callback_->Disconnect();
        wrapped_callback_.reset();
      }
      return delegate_->RegisterEncodeCompleteCallback(nullptr);
    }
  }

  int32_t Release() override {
    webrtc::VideoEncoder* encoder = nullptr;
    CVPixelBufferPoolRef pool_to_release = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      initialized_ = false;
      pool_to_release = pixel_buffer_pool_;
      pixel_buffer_pool_ = nullptr;
      pool_width_ = 0;
      pool_height_ = 0;
      if (!released_ && delegate_) {
        encoder = delegate_.get();
      }
    }
    if (pool_to_release) {
      CVPixelBufferPoolRelease(pool_to_release);
    }
    int32_t res = WEBRTC_VIDEO_CODEC_OK;
    if (encoder) {
      res = encoder->Release();
    }
    return res;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    if (frame.width() <= 0 || frame.height() <= 0 ||
        frame.width() > 8192 || frame.height() > 8192) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    webrtc::VideoEncoder* encoder = nullptr;
    bool needs_reinit = false;
    webrtc::VideoCodec reinit_codec_settings;
    webrtc::VideoEncoder::Settings reinit_settings(
        webrtc::VideoEncoder::Capabilities(false), 1, 1200);
    bool use_settings_overload = false;
    int32_t number_of_cores = 1;
    size_t max_payload_size = 1200;
    {
      absl::MutexLock lock(&mutex_);
      if (released_ || !initialized_ || !delegate_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
      }
      if (frame.width() != codec_settings_.width || frame.height() != codec_settings_.height) {
        codec_settings_.width = static_cast<uint16_t>(frame.width());
        codec_settings_.height = static_cast<uint16_t>(frame.height());
        reinit_codec_settings = codec_settings_;
        reinit_settings = settings_;
        use_settings_overload = use_settings_overload_;
        number_of_cores = number_of_cores_;
        max_payload_size = max_payload_size_;
        needs_reinit = true;
      }
      encoder = delegate_.get();
    }

    std::vector<webrtc::VideoFrameType> keyframe_types = {
        webrtc::VideoFrameType::kVideoFrameKey};
    std::vector<webrtc::VideoFrameType> default_delta_types = {
        webrtc::VideoFrameType::kVideoFrameDelta};
    const std::vector<webrtc::VideoFrameType>* effective_frame_types =
        frame_types ? frame_types : &default_delta_types;

    if (needs_reinit) {
      int32_t init_res = use_settings_overload
                             ? encoder->InitEncode(&reinit_codec_settings, reinit_settings)
                             : encoder->InitEncode(&reinit_codec_settings, number_of_cores,
                                                   max_payload_size);
      {
        absl::MutexLock lock(&mutex_);
        if (released_ || !delegate_) {
          initialized_ = false;
          return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        if (init_res != WEBRTC_VIDEO_CODEC_OK) {
          initialized_ = false;
          return init_res;
        }
        if (wrapped_callback_ != nullptr) {
          delegate_->RegisterEncodeCompleteCallback(wrapped_callback_.get());
        }
      }
      effective_frame_types = &keyframe_types;
    }

    // Fast path for NV12 frames: copy planes directly into an IOSurface-backed
    // CVPixelBufferRef wrapped in RTCCVPixelBuffer (Type::kNative) so WebRTC's
    // ObjC bridge does not convert NV12 -> I420 -> NV12 on every frame.
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer = frame.video_frame_buffer();
    if (frame_buffer && frame_buffer->type() == webrtc::VideoFrameBuffer::Type::kNV12) {
      webrtc::VideoFrame native_frame = ConvertNV12ToNativeFrame(frame, frame_buffer->GetNV12());
      return encoder->Encode(native_frame, effective_frame_types);
    }

    // Call Encode on the raw pointer outside the lock.
    return encoder->Encode(frame, effective_frame_types);
  }

  void SetRates(const RateControlParameters& parameters) override {
    webrtc::VideoEncoder* encoder = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      if (!released_ && delegate_) {
        if (parameters.bitrate.get_sum_kbps() > 0) {
          codec_settings_.startBitrate = parameters.bitrate.get_sum_kbps();
        }
        if (parameters.framerate_fps > 0) {
          codec_settings_.maxFramerate = static_cast<uint32_t>(parameters.framerate_fps);
        }
        encoder = delegate_.get();
      }
    }
    if (encoder) {
      encoder->SetRates(parameters);
    }
  }

  void OnPacketLossRateUpdate(float packet_loss_rate) override {
    absl::MutexLock lock(&mutex_);
    if (!released_ && delegate_) delegate_->OnPacketLossRateUpdate(packet_loss_rate);
  }

  void OnRttUpdate(int64_t rtt_ms) override {
    absl::MutexLock lock(&mutex_);
    if (!released_ && delegate_) delegate_->OnRttUpdate(rtt_ms);
  }

  void OnLossNotification(const LossNotification& loss_notification) override {
    absl::MutexLock lock(&mutex_);
    if (!released_ && delegate_) delegate_->OnLossNotification(loss_notification);
  }

  void SetFecControllerOverride(webrtc::FecControllerOverride* fec_controller_override) override {
    absl::MutexLock lock(&mutex_);
    if (!released_ && delegate_) delegate_->SetFecControllerOverride(fec_controller_override);
  }

  EncoderInfo GetEncoderInfo() const override {
    absl::MutexLock lock(&mutex_);
    if (released_ || !delegate_) return EncoderInfo();
    return delegate_->GetEncoderInfo();
  }

 private:
  int32_t InitEncodeInternal(const webrtc::VideoCodec* codec_settings,
                             int32_t number_of_cores,
                             size_t max_payload_size,
                             const webrtc::VideoEncoder::Settings* settings) {
    if (!codec_settings || codec_settings->width == 0 || codec_settings->height == 0 ||
        codec_settings->width > 8192 || codec_settings->height > 8192) {
      absl::MutexLock lock(&mutex_);
      initialized_ = false;
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    webrtc::VideoEncoder* encoder = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      if (released_ || !delegate_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
      }
      codec_settings_ = *codec_settings;
      number_of_cores_ = number_of_cores;
      max_payload_size_ = max_payload_size;
      if (settings) {
        settings_ = *settings;
        use_settings_overload_ = true;
      } else {
        use_settings_overload_ = false;
      }
      encoder = delegate_.get();
    }
    int32_t res = settings ? encoder->InitEncode(codec_settings, *settings)
                           : encoder->InitEncode(codec_settings, number_of_cores, max_payload_size);
    {
      absl::MutexLock lock(&mutex_);
      if (released_ || !delegate_) {
        initialized_ = false;
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
      }
      if (res == WEBRTC_VIDEO_CODEC_OK && wrapped_callback_ != nullptr) {
        delegate_->RegisterEncodeCompleteCallback(wrapped_callback_.get());
      }
      initialized_ = (res == WEBRTC_VIDEO_CODEC_OK);
    }
    return res;
  }

  webrtc::VideoFrame ConvertNV12ToNativeFrame(const webrtc::VideoFrame& frame,
                                              const webrtc::NV12BufferInterface* nv12) {
    if (!nv12) {
      return frame;
    }
    const int width = nv12->width();
    const int height = nv12->height();

    CVPixelBufferPoolRef pool = nullptr;
    CVPixelBufferPoolRef old_pool = nullptr;
    {
      absl::MutexLock lock(&mutex_);
      if (released_) {
        return frame;
      }
      if (!pixel_buffer_pool_ || pool_width_ != width || pool_height_ != height) {
        old_pool = pixel_buffer_pool_;
        pixel_buffer_pool_ = nullptr;
        NSDictionary* pixel_buffer_attributes = @{
#if !defined(__aarch64__)
          (NSString*)kCVPixelBufferOpenGLCompatibilityKey : @(YES),
#endif
          (NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
          (NSString*)kCVPixelBufferPixelFormatTypeKey :
              @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
          (NSString*)kCVPixelBufferWidthKey : @(width),
          (NSString*)kCVPixelBufferHeightKey : @(height),
        };
        CVReturn pool_res = CVPixelBufferPoolCreate(
            kCFAllocatorDefault, nullptr,
            (__bridge CFDictionaryRef)pixel_buffer_attributes, &pixel_buffer_pool_);
        if (pool_res == kCVReturnSuccess && pixel_buffer_pool_) {
          pool_width_ = width;
          pool_height_ = height;
        } else {
          pixel_buffer_pool_ = nullptr;
          pool_width_ = 0;
          pool_height_ = 0;
        }
      }
      if (pixel_buffer_pool_) {
        pool = CVPixelBufferPoolRetain(pixel_buffer_pool_);
      }
    }
    if (old_pool) {
      CVPixelBufferPoolRelease(old_pool);
    }
    if (!pool) {
      return frame;
    }

    CVPixelBufferRef pixel_buffer = nullptr;
    CVReturn cv_ret = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pixel_buffer);
    CVPixelBufferPoolRelease(pool);
    if (cv_ret != kCVReturnSuccess || !pixel_buffer) {
      return frame;
    }

    if (CVPixelBufferLockBaseAddress(pixel_buffer, 0) != kCVReturnSuccess) {
      CVPixelBufferRelease(pixel_buffer);
      return frame;
    }

    uint8_t* dst_y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
    int dst_stride_y = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0));
    uint8_t* dst_uv = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
    int dst_stride_uv = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1));

    int copy_res = libyuv::NV12Copy(nv12->DataY(), nv12->StrideY(),
                                    nv12->DataUV(), nv12->StrideUV(),
                                    dst_y, dst_stride_y,
                                    dst_uv, dst_stride_uv,
                                    width, height);
    CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

    if (copy_res != 0) {
      CVPixelBufferRelease(pixel_buffer);
      return frame;
    }

    RTC_OBJC_TYPE(RTCCVPixelBuffer)* rtc_pixel_buffer =
        [[RTC_OBJC_TYPE(RTCCVPixelBuffer) alloc] initWithPixelBuffer:pixel_buffer];
    CVPixelBufferRelease(pixel_buffer);

    webrtc::VideoFrame native_frame(frame);
    native_frame.set_video_frame_buffer(
        webrtc::ObjCToNativeVideoFrameBuffer(rtc_pixel_buffer));
    return native_frame;
  }

  std::unique_ptr<webrtc::VideoEncoder> delegate_;
  bool released_ ABSL_GUARDED_BY(mutex_);
  bool initialized_ ABSL_GUARDED_BY(mutex_);
  bool use_settings_overload_ ABSL_GUARDED_BY(mutex_);
  int32_t number_of_cores_ ABSL_GUARDED_BY(mutex_);
  size_t max_payload_size_ ABSL_GUARDED_BY(mutex_);
  webrtc::VideoCodec codec_settings_ ABSL_GUARDED_BY(mutex_);
  webrtc::VideoEncoder::Settings settings_ ABSL_GUARDED_BY(mutex_);
  CVPixelBufferPoolRef pixel_buffer_pool_ ABSL_GUARDED_BY(mutex_);
  int pool_width_ ABSL_GUARDED_BY(mutex_);
  int pool_height_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  std::unique_ptr<FrameTrackingCallbackWrapper> wrapped_callback_ ABSL_GUARDED_BY(mutex_);
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
 *    @c DeferredReleaseVideoEncoder to prevent Use-After-Free crashes during stream renegotiation.
 */
class CompositeVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  CompositeVideoEncoderFactory(std::unique_ptr<webrtc::VideoEncoderFactory> hardware_factory,
                               std::unique_ptr<webrtc::VideoEncoderFactory> software_factory)
      : hardware_factory_(std::move(hardware_factory)),
        software_factory_(std::move(software_factory)) {}

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
      if (format.name == "H264") {
        webrtc::SdpVideoFormat elevated_format = format;
        auto it = elevated_format.parameters.find("profile-level-id");
        if (it != elevated_format.parameters.end() && it->second.size() == 6) {
          // Preserve the requested H.264 profile (e.g. 42e0 Baseline, 4d00 Main,
          // 640c High) while elevating the level to 5.2 ("34") to support high
          // resolutions like 1080x2400.
          elevated_format.parameters["profile-level-id"] = it->second.substr(0, 4) + "34";
        } else {
          elevated_format.parameters["profile-level-id"] = "640c34";
        }
        if (elevated_format.parameters.find("packetization-mode") ==
            elevated_format.parameters.end()) {
          elevated_format.parameters["packetization-mode"] = "1";
        }
        if (elevated_format.parameters.find("level-asymmetry-allowed") ==
            elevated_format.parameters.end()) {
          elevated_format.parameters["level-asymmetry-allowed"] = "1";
        }
        auto encoder = hardware_factory_->Create(env, elevated_format);
        if (encoder) {
          return std::make_unique<DeferredReleaseVideoEncoder>(std::move(encoder));
        }
      }
      for (const auto& hw_format : hardware_factory_->GetSupportedFormats()) {
        if (hw_format.IsSameCodec(format)) {
          auto encoder = hardware_factory_->Create(env, format);
          if (encoder) {
            return std::make_unique<DeferredReleaseVideoEncoder>(std::move(encoder));
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
                               std::unique_ptr<webrtc::VideoDecoderFactory> software_factory)
      : hardware_factory_(std::move(hardware_factory)),
        software_factory_(std::move(software_factory)) {}

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
          auto decoder = hardware_factory_->Create(env, format);
          if (decoder) {
            return decoder;
          }
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
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePlatformVideoEncoderFactory() {
  const char* encoder_env = std::getenv("ANDROID_EMU_VIDEO_ENCODER");
  std::string override_backend = encoder_env ? absl::AsciiStrToLower(encoder_env) : "";

  std::unique_ptr<webrtc::VideoEncoderFactory> hardware_factory = nullptr;
  if (override_backend == "software" || override_backend == "sw" ||
      override_backend == "builtin") {
    RTC_LOG(LS_INFO) << "ANDROID_EMU_VIDEO_ENCODER explicitly configured to 'software'.";
  } else {
    RTCVideoEncoderFactoryH264* objc_encoder_factory = [[RTCVideoEncoderFactoryH264 alloc] init];
    hardware_factory = webrtc::ObjCToNativeVideoEncoderFactory(objc_encoder_factory);
  }

  auto software_factory = webrtc::CreateBuiltinVideoEncoderFactory();
  return std::make_unique<CompositeVideoEncoderFactory>(
      std::move(hardware_factory), std::move(software_factory));
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePlatformVideoDecoderFactory() {
  RTCVideoDecoderFactoryH264* objc_decoder_factory = [[RTCVideoDecoderFactoryH264 alloc] init];
  auto hardware_factory = webrtc::ObjCToNativeVideoDecoderFactory(objc_decoder_factory);
  auto software_factory = webrtc::CreateBuiltinVideoDecoderFactory();

  return std::make_unique<CompositeVideoDecoderFactory>(
      std::move(hardware_factory), std::move(software_factory));
}

}  // namespace goldfish::videobridge

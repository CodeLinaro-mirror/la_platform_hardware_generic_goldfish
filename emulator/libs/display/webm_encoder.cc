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

#include "goldfish/display/webm_encoder.h"

#include "absl/log/log.h"

namespace goldfish::display {

WebMEncoder::WebMEncoder(const std::string& fname, int w, int h, int f, int br)
        : filename(fname), width(w), height(h), fps(f), bitrate(br) {
    // Constructor is now safe and lightweight
    VLOG(2) << "[Encoder] VP9 Codec  w " << w << " h " << h << " f " << f << " br " << br;
}

WebMEncoder::~WebMEncoder() {
    if (is_initialized && !finished) {
        finish();
    }
    // Join thread if it's still running (just in case finish wasn't called explicitly)
    if (encoderThread.joinable()) {
        encoderThread.join();
    }
    cleanup();
}

bool WebMEncoder::init() {
    // 1. Alloc Output Context
    avformat_alloc_output_context2(&fmt_ctx, nullptr, "webm", filename.c_str());
    if (!fmt_ctx) {
        LOG(WARNING) << "[Encoder] Failed to create output context";
        return false;
    }

    // 2. Find Encoder (VP9)
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
    if (!codec) {
        LOG(WARNING) << "[Encoder] VP9 Codec not found";
        cleanup();
        return false;
    }

    // 3. Add Stream
    stream = avformat_new_stream(fmt_ctx, codec);
    if (!stream) {
        LOG(WARNING) << "[Encoder] Failed to allocate stream";
        cleanup();
        return false;
    }
    stream->id = fmt_ctx->nb_streams - 1;

    // 4. Alloc Codec Context
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        LOG(WARNING) << "[Encoder] Failed to alloc codec context";
        cleanup();
        return false;
    }

    // 5. Set Parameters
    // We are intentionally downscaling the resolution by half to reduce the
    // encoding bitrate and computational overhead, which is beneficial for
    // real-time streaming or recording. The sws_scale will handle the
    // downscaling from the input RGB frame size to this codec context size.
    codec_ctx->width = width / 2;
    codec_ctx->height = height / 2;
    codec_ctx->time_base = {1, fps};
    codec_ctx->framerate = {fps, 1};
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->bit_rate = bitrate;
    codec_ctx->gop_size = 10;

    // VP9 Speed Settings
    av_opt_set(codec_ctx->priv_data, "deadline", "realtime", 0);
    av_opt_set(codec_ctx->priv_data, "cpu-used", "4", 0);

    // 6. Open Codec
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        LOG(WARNING) << "[Encoder] Failed to open codec";
        cleanup();
        return false;
    }
    avcodec_parameters_from_context(stream->codecpar, codec_ctx);

    // 7. Open Output File
    if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
        LOG(WARNING) << "[Encoder] Failed to open output file: " << filename;
        cleanup();
        return false;
    }

    // 8. Write Header
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        LOG(WARNING) << "[Encoder] Failed to write header";
        cleanup();
        return false;
    }

    // 9. Allocate Frames & Packet
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        LOG(WARNING) << "[Encoder] Failed to alloc frame/packet";
        cleanup();
        return false;
    }

    frame->format = codec_ctx->pix_fmt;
    frame->width = width;
    frame->height = height;
    if (av_frame_get_buffer(frame, 32) < 0) {
        LOG(WARNING) << "[Encoder] Failed to alloc frame buffer";
        cleanup();
        return false;
    }

    // 10. Init SwsContext (RGB -> YUV)
    sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24, width / 2, height / 2,
                             AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        LOG(WARNING) << "[Encoder] Failed to init sws_ctx";
        cleanup();
        return false;
    }

    // 11. Start Thread
    is_initialized = true;
    encoderThread = std::thread(&WebMEncoder::encodeLoop, this);

    return true;
}

void WebMEncoder::addFrame(const uint8_t* rgbData) {
    {
        absl::MutexLock lock(queueMutex);
        if (!is_initialized || stopSignal) return;
    }

    size_t dataSize = width * height * 3;
    std::vector<uint8_t> frameData(rgbData, rgbData + dataSize);

    {
        absl::MutexLock lock(queueMutex);
        frameQueue.push(std::move(frameData));
    }
}

void WebMEncoder::finish() {
    if (!is_initialized || finished) return;

    // Signal thread
    {
        absl::MutexLock lock(queueMutex);
        stopSignal = true;
    }

    VLOG(2) << " WebMEncoder: finish wait for encoderThread ";
    if (encoderThread.joinable()) {
        encoderThread.join();
    }
    VLOG(2) << " WebMEncoder: done finish wait for encoderThread ";
    finished = true;
}

bool WebMEncoder::isFrameAvailable() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(queueMutex) {
    return !frameQueue.empty() || stopSignal;
}

void WebMEncoder::encodeLoop() {
    int count = 0;
    while (true) {
        ++count;
        VLOG(2) << "loop " << count;
        std::vector<uint8_t> currentData;

        {
            absl::MutexLock lock(queueMutex);
            VLOG(2) << "loop " << count << " wait for new frame ";
            queueMutex.Await(absl::Condition(this, &WebMEncoder::isFrameAvailable));

            VLOG(2) << "loop " << count << " got new frame ";
            if (stopSignal) {
                // currently, just try to wrap up and return as quickly as possible
                // TODO: Consider processing the remaining frames in frameQueue
                // before exiting to ensure all enqueued frames are encoded.
                VLOG(2) << "loop " << count << " exit now " << " stopSignal "
                        << " there are frames left: " << frameQueue.size();
                break;
            }
            currentData = std::move(frameQueue.front());
            frameQueue.pop();
        }

        processFrame(currentData, ptsCounter++);
    }

    // Flush
    VLOG(2) << "send null frame to end encoder";
    avcodec_send_frame(codec_ctx, nullptr);
    while (true) {
        VLOG(2) << "wait for package ";
        int ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            VLOG(2) << "no more package ";
            break;
        }

        VLOG(2) << "got package ";

        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(fmt_ctx);

    VLOG(2) << "finally clean up encoder context";
    // Close file IO context
    if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_ctx->pb);
    }
    VLOG(2) << "done";
}

void WebMEncoder::processFrame(const std::vector<uint8_t>& rawData, int64_t frameIndex) {
    if (av_frame_make_writable(frame) < 0) return;

    const uint8_t* srcSlice[] = {rawData.data()};
    const int srcStride[] = {width * 3};

    sws_scale(sws_ctx, srcSlice, srcStride, 0, height, frame->data, frame->linesize);

    frame->pts = frameIndex;

    if (avcodec_send_frame(codec_ctx, frame) < 0) return;

    while (true) {
        int ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }
}

void WebMEncoder::cleanup() {
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (pkt) {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx) {
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
    }
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    is_initialized = false;
}
}  // namespace goldfish::display

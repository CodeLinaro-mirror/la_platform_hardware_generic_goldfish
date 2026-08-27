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
#include "goldfish/grpcweb/grpc_web_server.h"

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/client_callback.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/grpcweb/grpc_web_protocol.h"
#include "goldfish/http/http_headers.h"
#include "goldfish/http/http_request.h"
#include "goldfish/http/http_response.h"
#include "goldfish/http/http_response_writer.h"
#include "goldfish/http/web_server.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::grpcweb {

namespace {

bool IsOriginAllowed(std::string_view origin, std::string_view default_allow_origin) {
    if (default_allow_origin == "*") {
        return true;
    }
    if (origin.empty()) {
        return false;
    }
    for (std::string_view allowed :
         absl::StrSplit(default_allow_origin, ',', absl::SkipWhitespace())) {
        if (absl::StripAsciiWhitespace(allowed) == origin) {
            return true;
        }
    }
    return false;
}

bool IsValidGrpcMethodPath(std::string_view path) {
    if (path.empty() || path[0] != '/') {
        return false;
    }
    size_t second_slash = path.find('/', 1);
    if (second_slash == std::string_view::npos || second_slash == 1 ||
        second_slash == path.size() - 1) {
        return false;
    }
    if (path.find('/', second_slash + 1) != std::string_view::npos) {
        return false;
    }
    for (char c : path) {
        if (absl::ascii_iscntrl(static_cast<unsigned char>(c)) ||
            absl::ascii_isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

http::HttpResponse HandleCorsPreflight(const http::HttpRequest& req,
                                       std::string_view default_allow_origin) {
    std::string_view req_origin = req.GetHeader(http::headers::kOrigin);
    if (!req_origin.empty() && !IsOriginAllowed(req_origin, default_allow_origin)) {
        VLOG(1) << "CORS preflight rejected: origin '" << req_origin << "' not in allowlist '"
                << default_allow_origin << "'";
        return http::HttpResponse::String("Forbidden: CORS origin not allowed",
                                          http::HttpStatus::kForbidden);
    }

    std::string origin_header_val;
    if (default_allow_origin == "*") {
        origin_header_val = req_origin.empty() ? "*" : std::string(req_origin);
    } else {
        origin_header_val = std::string(req_origin);
    }

    std::string_view allow_headers = req.GetHeader(http::headers::kAccessControlRequestHeaders);
    if (allow_headers.empty()) {
        allow_headers = "content-type,x-grpc-web,x-user-agent,authorization";
    }

    return http::HttpResponse::Empty(http::HttpStatus::kNoContent)
            .WithHeader(http::headers::kAccessControlAllowOrigin, origin_header_val)
            .WithHeader(http::headers::kAccessControlAllowMethods, "POST, OPTIONS")
            .WithHeader(http::headers::kAccessControlAllowHeaders, allow_headers)
            .WithHeader(http::headers::kAccessControlAllowCredentials, http::protocol::kTrue)
            .WithHeader(http::headers::kAccessControlMaxAge, "1728000")
            .WithHeader(http::headers::kVary, "Origin");
}

constexpr std::string_view kGrpcExposeHeaders =
        "grpc-status, grpc-message, grpc-status-details-bin";
constexpr std::string_view kNoCacheControl = "no-cache, no-store, max-age=0, must-revalidate";

absl::flat_hash_map<std::string, std::string> FormatHttp200Headers(
        const http::HttpRequest& req, bool is_text_response,
        std::string_view default_allow_origin) {
    absl::flat_hash_map<std::string, std::string> headers;
    headers.emplace(http::headers::kContentType, is_text_response
                                                         ? http::mime::kApplicationGrpcWebTextProto
                                                         : http::mime::kApplicationGrpcWebProto);
    headers.emplace(http::headers::kAccessControlExposeHeaders, kGrpcExposeHeaders);
    headers.emplace(http::headers::kCacheControl, kNoCacheControl);
    headers.emplace(http::headers::kTransferEncoding, "chunked");

    std::string_view req_origin = req.GetHeader(http::headers::kOrigin);
    if (!req_origin.empty() && IsOriginAllowed(req_origin, default_allow_origin)) {
        headers.emplace(http::headers::kAccessControlAllowOrigin, req_origin);
        headers.emplace(http::headers::kAccessControlAllowCredentials, http::protocol::kTrue);
        headers.emplace(http::headers::kVary, "Origin");
    } else if (default_allow_origin == "*") {
        headers.emplace(http::headers::kAccessControlAllowOrigin, "*");
    }

    return headers;
}

class GrpcWebReactor : public grpc::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer> {
  public:
    GrpcWebReactor(std::shared_ptr<http::HttpResponseWriter> writer,
                   std::shared_ptr<grpc::ClientContext> context, std::string method,
                   absl::flat_hash_map<std::string, std::string> http_headers,
                   bool is_text_response, bool has_request_payload, grpc::ByteBuffer write_bb)
            : writer_(std::move(writer))
            , context_(std::move(context))
            , method_(std::move(method))
            , http_headers_(std::move(http_headers))
            , is_text_response_(is_text_response)
            , has_request_payload_(has_request_payload)
            , write_bb_(std::move(write_bb)) {}

    const std::string& Method() const { return method_; }

    void Start() {
        if (has_request_payload_) {
            StartWriteLast(&write_bb_, grpc::WriteOptions());
        } else {
            StartWritesDone();
        }
        StartRead(&read_bb_);
        StartCall();
    }

    void OnReadInitialMetadataDone(bool /*ok*/) override {
        const absl::MutexLock lock(&mutex_);
        MaybeSendHeaders();
    }

    void OnReadDone(bool ok) override {
        if (ok) {
            std::string resp_data = Protocol::ByteBufferToString(read_bb_);
            std::string data_frame = Protocol::PackFrame(kFrameData, resp_data);
            if (data_frame.empty() && !resp_data.empty()) {
                LOG(ERROR) << "gRPC payload exceeded maximum frame size: " << resp_data.size();
                context_->TryCancel();
                return;
            }
            std::string chunk_to_send;
            if (is_text_response_) {
                chunk_to_send = b64_encoder_.EncodeChunk(data_frame);
            } else {
                chunk_to_send = std::move(data_frame);
            }
            {
                const absl::MutexLock lock(&mutex_);
                MaybeSendHeaders();
                if (!chunk_to_send.empty()) {
                    writer_->SendChunk(chunk_to_send);
                }
            }
            StartRead(&read_bb_);
        }
    }

    void OnDone(const grpc::Status& status) override {
        if (!status.ok()) {
            VLOG(1) << "gRPC call " << method_
                    << " finished with status: code=" << status.error_code() << " msg='"
                    << status.error_message() << "'";
        }

        std::string trailers =
                Protocol::FormatTrailers(status, context_->GetServerTrailingMetadata());
        std::string trailer_frame = Protocol::PackFrame(kFrameTrailer, trailers);
        std::string trailer_chunk;
        if (is_text_response_) {
            trailer_chunk = b64_encoder_.Finalize(trailer_frame);
        } else {
            trailer_chunk = std::move(trailer_frame);
        }
        {
            const absl::MutexLock lock(&mutex_);
            MaybeSendHeaders();
            if (!trailer_chunk.empty()) {
                writer_->SendChunk(trailer_chunk);
            }
            writer_->Finish();
        }
        delete this;
    }

  private:
    void MaybeSendHeaders() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
        if (!headers_sent_) {
            headers_sent_ = true;
            writer_->SendHeaders(http_headers_, 200);
        }
    }

    std::shared_ptr<http::HttpResponseWriter> writer_;
    std::shared_ptr<grpc::ClientContext> context_;
    std::string method_;
    absl::flat_hash_map<std::string, std::string> http_headers_;
    bool is_text_response_ = false;
    bool has_request_payload_ = false;
    absl::Mutex mutex_;
    bool headers_sent_ ABSL_GUARDED_BY(mutex_) = false;
    grpc::ByteBuffer write_bb_;
    grpc::ByteBuffer read_bb_;
    StreamingBase64Encoder b64_encoder_;
};

void HandleGrpcWebStream(const http::HttpRequest& req,
                         std::shared_ptr<http::HttpResponseWriter> writer,
                         std::shared_ptr<grpc::Channel> channel, std::string default_allow_origin) {
    auto send_error = [&](http::HttpStatus status, std::string_view msg) {
        absl::flat_hash_map<std::string, std::string> err_headers;
        err_headers.emplace(http::headers::kContentType, http::mime::kTextPlain);
        writer->SendHeaders(err_headers, static_cast<int>(status));
        writer->SendChunk(msg);
        writer->Finish();
    };

    if (!IsValidGrpcMethodPath(req.Path())) {
        VLOG(1) << "Malformed gRPC method path: " << req.Path();
        send_error(http::HttpStatus::kNotFound, "Not Found: invalid gRPC method path");
        return;
    }

    std::string_view req_origin = req.GetHeader(http::headers::kOrigin);
    if (!req_origin.empty() && !IsOriginAllowed(req_origin, default_allow_origin)) {
        VLOG(1) << "Rejected unauthorized cross-origin request: origin='" << req_origin
                << "' allowed='" << default_allow_origin << "' path='" << req.Path() << "'";
        send_error(http::HttpStatus::kForbidden, "Forbidden: CORS origin not allowed");
        return;
    }

    std::string_view content_type = req.GetHeader(http::headers::kContentType);
    if (content_type.empty() ||
        (!absl::StartsWithIgnoreCase(content_type, http::mime::kApplicationGrpcWeb) &&
         !absl::StartsWithIgnoreCase(content_type, http::mime::kApplicationGrpcWebText))) {
        VLOG(1) << "Rejected request with invalid Content-Type for " << req.Path();
        send_error(http::HttpStatus::kUnsupportedMediaType,
                   "Unsupported Media Type (application/grpc-web required)");
        return;
    }

    bool is_text_request =
            absl::StartsWithIgnoreCase(content_type, http::mime::kApplicationGrpcWebText);
    std::string_view accept = req.GetHeader(http::headers::kAccept);
    bool is_text_response =
            is_text_request ||
            (!accept.empty() &&
             absl::StartsWithIgnoreCase(accept, http::mime::kApplicationGrpcWebText));

    // 1. Base64 Decode if text format
    std::string binary_body;
    std::string_view frame_bytes = req.Body();
    if (is_text_request) {
        if (!absl::Base64Unescape(absl::StripAsciiWhitespace(req.Body()), &binary_body)) {
            VLOG(1) << "Failed to decode base64 payload for " << req.Path();
            send_error(http::HttpStatus::kBadRequest, "Invalid base64 payload");
            return;
        }
        frame_bytes = binary_body;
    }

    // 2. Unpack 5-byte header to isolate raw Protobuf bytecode
    grpc::ByteBuffer request_bb;
    bool has_request_frame = false;
    if (!frame_bytes.empty()) {
        auto frame_opt = Protocol::UnpackFrame(frame_bytes);
        if (!frame_opt.has_value()) {
            VLOG(1) << "Malformed gRPC-Web framing header for " << req.Path();
            send_error(http::HttpStatus::kBadRequest, "Malformed gRPC-Web framing header");
            return;
        }
        has_request_frame = true;
        request_bb = Protocol::StringToByteBuffer(frame_opt->payload);
    }

    if (!channel) {
        LOG(ERROR) << "gRPC upstream channel unavailable for " << req.Path();
        send_error(http::HttpStatus::kServiceUnavailable, "gRPC upstream channel unavailable");
        return;
    }

    auto context = std::make_shared<grpc::ClientContext>();
    writer->SetOnCloseCallback([context]() {
        VLOG(1) << "Client disconnected, canceling upstream gRPC call";
        context->TryCancel();
    });

    std::string_view timeout_str = req.GetHeader(http::headers::kGrpcTimeout);
    if (!timeout_str.empty()) {
        auto deadline = Protocol::ParseGrpcTimeout(timeout_str);
        if (deadline != absl::InfiniteFuture()) {
            context->set_deadline(absl::ToChronoTime(deadline));
        }
    }

    for (const auto& [k, v] : req.Headers()) {
        if (absl::StartsWithIgnoreCase(k, kHeaderPrefixCustom) ||
            absl::EqualsIgnoreCase(k, http::headers::kAuthorization) ||
            absl::EqualsIgnoreCase(k, http::headers::kProxyAuthorization)) {
            context->AddMetadata(absl::AsciiStrToLower(k), v);
        }
    }

    auto http_headers = FormatHttp200Headers(req, is_text_response, default_allow_origin);
    std::string method = std::string(req.Path());
    auto* reactor = new GrpcWebReactor(writer, context, std::move(method), std::move(http_headers),
                                       is_text_response, has_request_frame, std::move(request_bb));

    grpc::GenericStub stub(channel);
    stub.PrepareBidiStreamingCall(context.get(), reactor->Method(), /*options=*/{}, reactor);
    reactor->Start();
}

}  // namespace

absl::StatusOr<std::unique_ptr<GrpcWebServer>> GrpcWebServer::Create(
        async::EventLoop* loop, std::shared_ptr<async::AsyncSocketFactory> socket_factory,
        const network::Endpoint& endpoint, std::shared_ptr<grpc::Channel> grpc_channel,
        std::string default_allow_origin, http::AccessLogger access_logger) {
    if (loop == nullptr) {
        return absl::InvalidArgumentError("EventLoop cannot be null");
    }
    if (socket_factory == nullptr) {
        return absl::InvalidArgumentError("AsyncSocketFactory cannot be null");
    }

    std::string ip_addr = "0.0.0.0";
    uint16_t port = static_cast<uint16_t>(network::GetPortFromEndpoint(endpoint));
    if (std::holds_alternative<network::Ipv4Endpoint>(endpoint)) {
        ip_addr = network::ToString(std::get<network::Ipv4Endpoint>(endpoint).addr);
    } else if (std::holds_alternative<network::Ipv6Endpoint>(endpoint)) {
        ip_addr = network::ToString(std::get<network::Ipv6Endpoint>(endpoint).addr);
    }

    auto config = http::CreateWebServer(port)
                          .BindAddress(std::move(ip_addr))
                          .EventLoop(loop)
                          .SocketFactory(std::move(socket_factory))
                          .MaxPayloadSize(kMaxFramePayloadSize);
    if (access_logger) {
        config.AccessLog(std::move(access_logger));
    }

    auto http_server = std::make_unique<http::WebServer>(std::move(config));

    http_server->OnOptions("/*", [default_allow_origin](const http::HttpRequest& req) {
        return HandleCorsPreflight(req, default_allow_origin);
    });

    http_server->OnStream(http::HttpMethod::kPost, "/*",
                          [grpc_channel = std::move(grpc_channel), default_allow_origin](
                                  const http::HttpRequest& req,
                                  std::shared_ptr<http::HttpResponseWriter> writer) {
                              HandleGrpcWebStream(req, std::move(writer), grpc_channel,
                                                  default_allow_origin);
                          });

    auto start_status = http_server->Start(false);
    if (!start_status.ok()) {
        return start_status;
    }

    return std::unique_ptr<GrpcWebServer>(new GrpcWebServer(std::move(http_server)));
}

GrpcWebServer::GrpcWebServer(std::unique_ptr<http::WebServer> http_server)
        : http_server_(std::move(http_server)) {}

GrpcWebServer::~GrpcWebServer() {
    Stop();
}

network::Endpoint GrpcWebServer::GetEndpoint() const {
    if (http_server_) {
        return http_server_->GetEndpoint();
    }
    return {};
}

size_t GrpcWebServer::ActiveSessionsCount() const {
    if (http_server_) {
        return http_server_->ActiveSessionsCount();
    }
    return 0;
}

void GrpcWebServer::Stop() {
    if (http_server_) {
        http_server_->Stop();
    }
}

}  // namespace goldfish::grpcweb

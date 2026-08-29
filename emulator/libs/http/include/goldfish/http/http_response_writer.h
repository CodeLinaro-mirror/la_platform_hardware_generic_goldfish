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

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"

#include "goldfish/http/http_headers.h"

namespace goldfish::http {

/**
 * @brief Abstract interface for asynchronous / streaming HTTP response generation.
 *
 * Allows handlers to stream headers, chunked payloads, and trailers asynchronously across
 * threads (such as from gRPC Callback reactors or event queues) without buffering the entire
 * stream in memory.
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/http/http_headers.h"
 * #include "goldfish/http/web_server.h"
 *
 * ws.OnStream(HttpMethod::kPost, "/grpc.service.Echo/Stream",
 *     [](const HttpRequest& req, std::shared_ptr<HttpResponseWriter> writer) {
 *         // 1. Send initial response headers (defaults to HttpStatus::kOk)
 *         writer->SendHeaders({
 *             {std::string(headers::kContentType), std::string(mime::kApplicationGrpcWeb)},
 *             {std::string(headers::kAccessControlAllowOrigin), "*"},
 *         });
 *
 *         // 2. Stream data chunks from a worker thread or async reactor
 *         writer->SendChunk("data_chunk_1");
 *         writer->SendChunk("data_chunk_2");
 *
 *         // 3. Listen for early client cancellation
 *         writer->SetOnCloseCallback([]() {
 *             LOG(INFO) << "Client disconnected prematurely";
 *         });
 *
 *         // 4. Complete the stream
 *         writer->Finish();
 *     });
 * @endcode
 */
class HttpResponseWriter {
  public:
    virtual ~HttpResponseWriter() = default;

    /**
     * @brief Sends HTTP response headers with a strongly-typed HttpStatus (defaults to
     * HttpStatus::kOk).
     *
     * If the headers include "transfer-encoding: chunked", SendChunk() will automatically apply
     * HTTP/1.1 chunked framing (<hex_length>\r\n<data>\r\n) and Finish() will emit the terminating
     * "0\r\n\r\n" chunk.
     * If transfer-encoding is omitted or set to identity, SendChunk() emits raw payload bytes
     * (close-delimited stream) and Finish() closes the socket upon stream completion.
     *
     * Threading: Thread-safe; safely marshals write to the socket's EventLoop thread.
     *
     * @param headers Map of header key-value pairs.
     * @param status Strongly typed HttpStatus enum (default: HttpStatus::kOk).
     */
    void SendHeaders(const absl::flat_hash_map<std::string, std::string>& headers,
                     HttpStatus status = HttpStatus::kOk) {
        SendHeaders(headers, static_cast<int>(status));
    }

    /**
     * @brief Sends HTTP response headers with a raw integer status code.
     *
     * Threading: Thread-safe; safely marshals write to the socket's EventLoop thread.
     *
     * @param headers Map of header key-value pairs.
     * @param status Integer HTTP status code.
     */
    virtual void SendHeaders(const absl::flat_hash_map<std::string, std::string>& headers,
                             int status) = 0;

    /**
     * @brief Transmits a data chunk/fragment to the client.
     *
     * If Transfer-Encoding: chunked was configured in response headers, applies RFC 9112
     * chunk framing (<hex_length>\r\n<data>\r\n). Otherwise transmits raw bytes.
     *
     * Threading: Thread-safe; safely marshals write to the socket's EventLoop thread.
     *
     * @param chunk Data payload fragment to transmit.
     */
    virtual void SendChunk(std::string_view chunk) = 0;

    /**
     * @brief Signals end of stream and completes the response.
     *
     * If chunked encoding was enabled, transmits the final terminating chunk ("0\r\n\r\n").
     * Records access log telemetry and terminates/resets the connection.
     *
     * Threading: Thread-safe; safely marshals close to the socket's EventLoop thread.
     */
    virtual void Finish() = 0;

    /**
     * @brief Registers a notification callback invoked when the client socket disconnects.
     *
     * Useful for early upstream cancellation (e.g. aborting long-running gRPC streaming calls).
     *
     * @param on_close Callback invoked upon socket disconnection.
     */
    virtual void SetOnCloseCallback(std::function<void()> on_close) = 0;
};

}  // namespace goldfish::http

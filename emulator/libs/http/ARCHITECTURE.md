# Component: HTTP Server Framework (`goldfish::http`)

**Role:** Lightweight, non-blocking C++20 HTTP/1.1 micro-framework designed for
embedded REST, access logging, and streaming handlers in the emulator stack.
**Location:** `hardware/generic/goldfish/emulator/libs/http`
**Namespace:** `goldfish::http`

---

## Integration Guide

| Class / Interface                                      | Bazel Target                         | Header Path                                    | Description                                                                                                                                                                      |
| :----------------------------------------------------- | :----------------------------------- | :--------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `HttpStatus`, `headers`, `methods`, `mime`, `protocol` | `//emulator/libs/http:http_messages` | `include/goldfish/http/http_headers.h`         | Strongly-typed `HttpStatus` enum, RFC 9110 reason phrases, standard lowercase header names, HTTP verbs, MIME media types, and wire framing tokens.                               |
| `HttpRequest`                                          | `//emulator/libs/http:http_messages` | `include/goldfish/http/http_request.h`         | Immutable representation of an incoming HTTP/1.1 request (method, path, headers, body) with zero-allocation fast-path lookups.                                                   |
| `HttpResponse`                                         | `//emulator/libs/http:http_messages` | `include/goldfish/http/http_response.h`        | Value type with fluent builders (`WithStatus`, `WithHeader`, `WithContentType`, `String`, `Empty`) supporting both `HttpStatus` and integer status codes.                        |
| `HttpResponseWriter`                                   | `//emulator/libs/http:http_messages` | `include/goldfish/http/http_response_writer.h` | Abstract streaming response writer interface for asynchronous chunked data delivery across threads and client disconnect detection (`SetOnCloseCallback`).                       |
| `HttpRouter`                                           | `//emulator/libs/http:http_router`   | `include/goldfish/http/http_router.h`          | Zero-allocation route registry supporting exact lookups, longest-prefix wildcards (`/*`, `/static/*`), and composite 405 Method Not Allowed discovery.                           |
| `AccessLogEntry`, `AccessLogger`                       | `//emulator/libs/http:http_session`  | `include/goldfish/http/http_session.h`         | Structured request/response access telemetry record with `absl::Time` timestamps, endpoints, status codes, payload bytes, and `absl::Duration` latencies.                        |
| `HttpSession`                                          | `//emulator/libs/http:http_session`  | `include/goldfish/http/http_session.h`         | Connection state machine wrapping `@llhttp` with Keep-Alive support, path query normalization, access logging, and early 413 limit enforcement.                                  |
| `CreateWebServer`, `WebServer`                         | `//emulator/libs/http:web_server`    | `include/goldfish/http/web_server.h`           | Top-level HTTP daemon supporting fluent builder configuration (`CreateWebServer`), validation (`Validate`), lambda routing (`OnGet`, `OnPost`, etc.), and thread-safe streaming. |
| **All Components**                                     | `//emulator/libs/http:http_server`   | `include/goldfish/http/web_server.h`           | Convenience umbrella library exporting the full HTTP server framework.                                                                                                           |

---

## Usage Examples

### 1. REST & Unary Handlers with Access Logging

```cpp
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "goldfish/http/http_headers.h"
#include "goldfish/http/web_server.h"

using namespace goldfish::http;

int main() {
    auto config = CreateWebServer(8080)
                      .BindAddress("127.0.0.1")
                      .MaxPayloadSize(32 * 1024 * 1024)
                      .AccessLog([](const AccessLogEntry& log) {
                          LOG(INFO) << log.client_endpoint.ToString() << " - ["
                                    << absl::FormatTime(absl::RFC3339_full, log.timestamp,
                                                        absl::LocalTimeZone())
                                    << "] \"" << HttpMethodToString(log.method) << " "
                                    << log.path << "\" " << log.status_code << " "
                                    << log.bytes_sent << "b in "
                                    << absl::FormatDuration(log.duration);
                      });

    WebServer ws{config};

    // Unary JSON endpoint
    ws.OnGet("/api/v1/info", [](const HttpRequest& req) {
        return HttpResponse::String("{\"version\":\"1.0\"}", HttpStatus::kOk,
                                    mime::kApplicationJson)
                .WithHeader(headers::kCacheControl, "no-cache");
    });

    // POST with body
    ws.OnPost("/api/v1/echo", [](const HttpRequest& req) {
        return HttpResponse::String(std::string(req.Body()), HttpStatus::kOk,
                                    req.GetHeader(headers::kContentType));
    });

    // Start server with [[nodiscard]] Status check
    auto status = ws.Start(/*blocking=*/true);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to start HTTP server: " << status;
        return 1;
    }
    return 0;
}
```

### 2. CORS Preflight Handlers

```cpp
    // Catch-all CORS OPTIONS preflight
    ws.OnOptions("/*", [](const HttpRequest& req) {
        return HttpResponse::Empty(HttpStatus::kNoContent)
                .WithHeader(headers::kAccessControlAllowOrigin, "*")
                .WithHeader(headers::kAccessControlAllowMethods, "POST, GET, OPTIONS")
                .WithHeader(headers::kAccessControlAllowHeaders, "content-type,x-grpc-web")
                .WithHeader(headers::kAccessControlMaxAge, "86400");
    });
```

### 3. Asynchronous Streaming & gRPC-Web Handlers

```cpp
    ws.OnStream(HttpMethod::kPost, "/grpc.service.User/*",
                [](const HttpRequest& req, std::shared_ptr<HttpResponseWriter> writer) {
                    writer->SendHeaders({
                            {std::string(headers::kContentType),
                             std::string(mime::kApplicationGrpcWebProto)},
                            {std::string(headers::kAccessControlAllowOrigin), "*"},
                    });

                    // Deliver chunks asynchronously from a worker thread or reactor
                    writer->SendChunk(grpc_payload_chunk);

                    // Listen for client aborts or disconnects
                    writer->SetOnCloseCallback([]() {
                        LOG(INFO) << "Client disconnected, aborting gRPC call";
                    });

                    // Complete the stream and emit access telemetry
                    writer->Finish();
                });
```

---

## Threading Model & Invariants

1. **EventLoop Confinement:** All socket I/O and `llhttp` parsing occurs
   strictly on the associated `goldfish::async::EventLoop` thread.
2. **Dijkstra Monitor Pattern:** Locks are never held across external callbacks,
   asynchronous posts, or socket close calls.
3. **Thread-Safe Response Streaming:** `HttpResponseWriter` methods
   (`SendHeaders`, `SendChunk`, `Finish`) safely marshal writes back to the
   `EventLoop` thread using `EventLoop::Post()`, allowing streaming writes from
   background threads (e.g. gRPC Callback reactors).
4. **Keep-Alive State Preservation:** When `HttpRequest::KeepAlive()` is true,
   `HttpSession` automatically resets parser state for subsequent requests on
   the same connection.
5. **Early 413 Payload Protection:** Incoming payloads exceeding
   `MaxPayloadSize()` are rejected early during `on_headers_complete` or
   streaming body ingestion without buffering into memory.
6. **Zero-Overhead Access Logging:** When `AccessLog` is configured, telemetry
   records are emitted per completed request without locking server mutexes or
   copying payload buffers.

---

## Dependencies

- **HTTP Parser:** `@llhttp` (Zero-allocation HTTP/1.1 parsing engine).
- **Async Engine:** `//emulator/libs/async:async_api`,
  `//emulator/libs/async:libuv_event_loop`,
  `//emulator/libs/async:libuv_sockets`.
- **Networking & Utilities:** `//emulator/libs/network`, `@abseil-cpp`
  (Containers, Mutex, Status, Strings, Time).

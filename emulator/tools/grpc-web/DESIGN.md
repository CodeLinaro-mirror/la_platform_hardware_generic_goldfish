# In-Process C++ gRPC-Web Proxy (`emulator/tools/grpc-web`)

**Component**: `hardware/generic/goldfish/emulator/tools/grpc-web`
**Status**: Draft Architecture Proposal
**Target Namespace**: `goldfish::grpcweb`

---

## 1. Objective & Motivation

To provide a high-performance, native C++17 **gRPC-Web to gRPC translation service** embedded directly within the emulator stack. This removes any requirement for external **Envoy** sidecars or proxy containers, allowing browser-based Web UIs (TypeScript / JavaScript `grpc-web`) to connect directly to the emulator over HTTP/1.1 or HTTP/2.

The implementation builds upon:
1. **`goldfish::http::WebServer`** for high-performance, non-blocking HTTP/1.1 routing, CORS handling, and chunked response streaming.
2. **`goldfish::async`** (`LibuvEventLoop`, `AsyncSocket`, `AsyncSocketServer`) for non-blocking network I/O.
3. **`grpc::GenericStub`** over an in-process or local gRPC channel to dynamically route any RPC method without static stub generation.

---

## 2. Architecture & Data Flow

```text
┌────────────────────────────────────────────────────────────────────────────┐
│                    goldfish::grpcweb Architecture                          │
│                                                                            │
│  [ Web Browser Client ] (grpc-web / XHR / Fetch)                           │
│         │ HTTP/1.1 (application/grpc-web[-text])                           │
│         ▼                                                                  │
│  [ goldfish::http::WebServer ] (:8080 or custom port)                      │
│         │                                                                  │
│         ▼                                                                  │
│  [ HTTP / CORS Route Handling ]                                            │
│     • OPTIONS Preflight ──> 204 No Content (Immediate return)              │
│     • POST /package.Service/Method ──> Extract gRPC-Web body               │
│     • Strip 5-byte header (0x00 flag + 4B big-endian length)               │
│     • Base64 decode (if application/grpc-web-text)                         │
│         │                                                                  │
│         ▼ grpc::ByteBuffer + Method Path                                   │
│  [ In-Process gRPC Bridge ]                                                │
│         │ grpc::GenericStub::PrepareBidiStreamingCall()                    │
│         ▼                                                                  │
│  [ Native gRPC Server (EmulatorController, RtcService, etc.) ]             │
│         │                                                                  │
│         ▼ Response ByteBuffers + grpc::Status                              │
│  [ gRPC-Web Outgoing Packager (GrpcWebReactor) ]                           │
│     • 0x00 Data Frames (4B big-endian payload length)                      │
│     • 0x80 In-Band Trailer Frame (grpc-status, grpc-message, metadata)     │
│     • Base64 encode (if text format requested)                             │
│         │                                                                  │
│         ▼ HttpResponseWriter::SendChunk() / Finish()                       │
│  [ WebServer / AsyncSocket ] ──> HTTP/1.1 200 OK Chunked Stream            │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Key Invariants & Threading Model

### 3.1 Strict Thread Isolation
* **EventLoop Thread**:
  * All `AsyncSocket` operations (`Send()`, `Close()`, `SetOnReadCallback()`) and `HttpSession` parsing run strictly on the socket's `LibuvEventLoop` thread.
* **gRPC Callback Reactor (`ClientBidiReactor`)**:
  * Instead of blocking threads or spawning unbounded detached worker threads, the server utilizes the modern **gRPC Callback API** (`grpc::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer>`).
  * gRPC's internal thread pool asynchronously invokes reactions (`OnReadDone`, `OnWriteDone`, `OnDone`).
  * When response chunks or trailers arrive, they are dispatched back to the socket's event loop via thread-safe `HttpResponseWriter` methods (`SendHeaders`, `SendChunk`, `Finish`).
  * If the client socket disconnects or closes, `writer->SetOnCloseCallback()` calls `context->TryCancel()`, immediately aborting upstream RPCs and preventing resource leaks.

### 3.2 Protocol Conformance
* **5-Byte Framing**:
  * Evaluated using `ntohl()` on ingress and `htonl()` on egress with `std::memcpy` for strict aliasing and endianness safety.
* **In-Band `0x80` Trailers**:
  * Transmitted as an in-band frame carrying lowercase HTTP headers:
    ```http
    grpc-status: 0\r\n
    grpc-message: OK\r\n
    grpc-status-details-bin: <base64>\r\n
    ```
  * Special characters in `grpc-message` are percent-encoded according to RFC 3986.
* **CORS Preflight & Security**:
  * Responds to `OPTIONS` with `204 No Content`, matching allowed headers, origins, and credentials.
  * Cross-origin POST requests are validated against the allowlist before any gRPC call is dispatched, rejecting unauthorized origins with `403 Forbidden`.
  * `Content-Length` headers exceeding 64 MiB are rejected early (`413 Payload Too Large`) without memory buffering.
  * Strict method path validation (`IsValidGrpcMethodPath`) prevents malformed RPC routing.

### 3.3 Observability & Lifecycle
* **Clean Signal Shutdown**: Uses `absl::Notification` to handle `SIGINT`/`SIGTERM`, safely terminating the listener, draining active sessions, and canceling in-flight upstream RPCs.

---

## 4. Usage & Examples

> For component architecture, target definitions, and integration guides, see **[ARCHITECTURE.md](ARCHITECTURE.md)**.

### 4.1 Emulator Auto-Discovery & Discovery Files

When an Android Emulator instance is running, it dynamically advertises its gRPC port and security tokens in a discovery file located in the runtime temporary directory:

* **macOS**: `~/Library/Caches/TemporaryItems/avd/running/pid_<pid>.ini`
* **Linux**: `/tmp/android-$USER/run/pid_<pid>.ini`

`grpc-web-proxy` natively scans for running emulators via `goldfish::discovery::EmulatorAdvertisement` when `--grpc_target` is omitted.

### 4.2 Running the Proxy via Bazel

#### 1. Automatic Discovery (Zero Configuration)
If an emulator is already running, simply launch the proxy and it will automatically discover the instance:
```bash
bazel run @goldfish//emulator/tools/grpc-web:grpc-web-proxy -- --http_port=8085
```

#### 2. Explicit Discovery File
Target a specific emulator instance using its discovery `.ini` file:
```bash
bazel run @goldfish//emulator/tools/grpc-web:grpc-web-proxy -- \
    --http_port=8085 \
    --discovery_file=~/Library/Caches/TemporaryItems/avd/running/pid_53930.ini
```

#### 3. Explicit Target & Verbosity Levels
Override discovery with a direct gRPC target address:
```bash
# Start the gRPC-Web proxy forwarding to an explicit target
bazel run @goldfish//emulator/tools/grpc-web:grpc-web-proxy -- \
    --http_address=0.0.0.0 \
    --http_port=8085 \
    --grpc_target=localhost:17576 \
    --allow_origin="*" \
    --verbose

# Granular Abseil verbosity levels (1 = RPC flow, 2 = headers/frames, 3 = raw chunks):
bazel run @goldfish//emulator/tools/grpc-web:grpc-web-proxy -- \
    --http_port=8085 \
    --v=3
```

### 4.3 Connecting via Web Browser (JavaScript / TypeScript)

Using the standard `@grpc/grpc-web` npm client library:

```typescript
import { EmulatorControllerPromiseClient } from './proto/emulator_controller_grpc_web_pb';
import { VmRunState } from './proto/emulator_controller_pb';

// Connect to the in-process proxy HTTP endpoint
const client = new EmulatorControllerPromiseClient('http://localhost:8085');

async function checkStatus() {
  const response = await client.getStatus(new VmRunState(), {});
  console.log('Emulator status:', response.toObject());
}
```

### 4.4 Testing with cURL

#### CORS Preflight (`OPTIONS`)
```bash
curl -i -X OPTIONS http://localhost:8085/android.emulation.control.EmulatorController/getStatus \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: POST" \
  -H "Access-Control-Request-Headers: content-type,x-grpc-web"
```
**Expected Response:** `HTTP/1.1 204 No Content` with `Access-Control-Allow-Origin: http://localhost:3000`.

#### Binary gRPC-Web Call (`POST`)
Send an empty 5-byte framed payload (`0x00` flag + 4 zero bytes for empty protobuf):
```bash
printf '\x00\x00\x00\x00\x00' | curl -i -X POST http://localhost:8085/android.emulation.control.EmulatorController/getStatus \
  -H "Content-Type: application/grpc-web+proto" \
  -H "X-Grpc-Web: 1" \
  --data-binary @-
```
**Expected Response:** `HTTP/1.1 200 OK` with binary protobuf payload containing the device status and trailing frame `grpc-status: 0`.

#### Text-Encoded gRPC-Web Call (`POST`)
Send Base64-encoded payload (`AAAAAAA=` is Base64 for `\x00\x00\x00\x00\x00`):
```bash
echo -n "AAAAAAA=" | curl -i -X POST http://localhost:8085/android.emulation.control.EmulatorController/getStatus \
  -H "Content-Type: application/grpc-web-text" \
  -H "Accept: application/grpc-web-text" \
  -H "X-Grpc-Web: 1" \
  --data-binary @-
```
**Expected Response:** `HTTP/1.1 200 OK` with Base64-encoded protobuf payload and final trailer chunk:
```http
HTTP/1.1 200 OK
Content-Type: application/grpc-web-text
Access-Control-Allow-Origin: *
Access-Control-Allow-Credentials: true
Access-Control-Expose-Headers: grpc-status,grpc-message,grpc-status-details-bin
Cache-Control: no-cache, no-store, max-age=0, must-revalidate
Connection: close

<base64 protobuf payload>gAAAAA9ncnBjLXN0YXR1czowDQo=
```
*(Decodes to trailer frame with `grpc-status: 0`).*

#### Server-Side Streaming Call (`streamScreenshot`)
Showcases real-time server-side streaming over HTTP/1.1 chunked transfer. An empty `ImageFormat` message defaults to `format = PNG`, `width = 0` (unscaled), `height = 0`, and `display = 0` (main display):

```bash
# Stream live PNG screenshots unbuffered (-N):
echo -n "AAAAAAA=" | curl -N -X POST http://localhost:8085/android.emulation.control.EmulatorController/streamScreenshot \
  -H "Content-Type: application/grpc-web-text" \
  -H "Accept: application/grpc-web-text" \
  -H "X-Grpc-Web: 1" \
  --data-binary @-

# Or binary streaming mode:
printf '\x00\x00\x00\x00\x00' | curl -N -X POST http://localhost:8085/android.emulation.control.EmulatorController/streamScreenshot --output - \
  -H "Content-Type: application/grpc-web+proto" \
  -H "X-Grpc-Web: 1" \
  --data-binary @-
```

**Expected Behavior & Automatic Cancellation:**
* Each time the emulated device renders a new display frame, the proxy receives an `Image` protobuf frame from the emulator and flushes it immediately to the client.
* Terminating `curl` (e.g. `Ctrl+C` or piped command exit) closes the TCP socket. The proxy immediately triggers `active_context_->TryCancel()`, terminating upstream streaming on the emulator and cleanly reclaiming resources.

### 4.5 Running Tests

Run all unit tests, integration tests, and static analysis:
```bash
bazel test @goldfish//emulator/tools/grpc-web/...
```

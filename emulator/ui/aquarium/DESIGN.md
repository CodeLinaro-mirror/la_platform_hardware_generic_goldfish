# Component Design: Aquarium (`emulator/ui/aquarium`)

**Component:** `hardware/generic/goldfish/emulator/ui/aquarium`  
**Package Name:** `@android/aquarium`  
**Target:** React 18/19 Component Library & Headless TypeScript SDK for Android
Emulator  
**Backend:** AEMU v2 gRPC Services over `goldfish::grpcweb` & WebRTC
`Switchboard`

---

## 1. Executive Summary & Philosophy

**Aquarium** is the official modern React component library and TypeScript SDK
for interacting with the Android Emulator in web browsers.

### Core Design Principles:

1. **Single UI Component Footprint:** The library provides **exactly one visual
   UI component**: `<EmulatorView />` (which mounts the WebRTC `<video>` display
   and handles mouse, multi-touch, stylus, and keyboard I/O).
2. **Zero External Sidecars:** Connects directly to the emulator's embedded
   `goldfish::grpcweb::GrpcWebServer` (HTTP/1.1 CORS) and WebRTC `Switchboard`.
   No Envoy or custom WebSocket gateways required.
3. **Headless Service APIs & React Hooks:** All other emulator features
   (Sensors, Posture, Location/GPS, Snapshots, VM lifecycle, Clipboard,
   Capabilities) are exposed as typed, framework-agnostic TypeScript API clients
   (`AquariumClient`) and lightweight React hooks (`useSensors`, `useLocation`,
   `useVm`, `useCapabilities`). Developers have complete freedom to design their
   own surrounding UI controls.
4. **Rich v2 Multi-Modal Input:** Supports multi-touch gestures (up to 10
   pointers), desktop mouse capture (relative deltas & button masks), stylus pen
   (pressure, tilt, orientation), and DOM/Android keycodes directly over WebRTC
   SCTP DataChannels.

---

## 2. Component Architecture & Data Flow

```text
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   React Application Layer                                   │
│                                                                                             │
│  <Aquarium.Root uri="http://localhost:8080" token="...">                                    │
│    ├── <Aquarium.Display displayId={0} />   ──> WebRTC <video> + Multi-Touch/Mouse Overlay  │
│    ├── <Aquarium.ControlBar />              ──> Home, Back, Recents, Power, Volume buttons  │
│    ├── <Aquarium.MultiDisplayTabs />        ──> Dynamic primary/secondary display switcher  │
│    └── <Aquarium.SensorsPanel />            ──> Fold angle, GPS, battery controls           │
└──────────────────────────────────────────────┬──────────────────────────────────────────────┘
                                               │
                         React Context / Hooks (useAquarium)
                                               │
┌──────────────────────────────────────────────▼──────────────────────────────────────────────┐
│                              AquariumCore (TypeScript SDK)                                  │
│                                                                                             │
│  ├── RtcController          ──> Manages RTCPeerConnection, JSEP negotiation, audio tracks   │
│  ├── InputSessionChannel    ──> Serializes v2 InputEvent protobufs over SCTP DataChannel    │
│  ├── CapabilitiesClient     ──> Queries displays, hardware features, and codecs             │
│  ├── ServiceClients         ──> VmService, SystemService, ScreenCaptureService              │
│  └── GrpcWebTransport       ──> Standard HTTP/1.1 @grpc/grpc-web transport via proxy        │
└──────────────────────────────────────────────┬──────────────────────────────────────────────┘
                                               │
                         HTTP/1.1 gRPC-Web (CORS) & WebRTC RTP/SCTP
                                               │
┌──────────────────────────────────────────────▼──────────────────────────────────────────────┐
│                               Android Emulator (C++ Host)                                   │
│                                                                                             │
│  ├── goldfish::grpcweb::GrpcWebServer  ──> In-process HTTP/1.1 to gRPC translator           │
│  ├── goldfish::grpc::v2::* Services    ──> RtcService, CapabilitiesService, InputService    │
│  └── goldfish::videobridge             ──> Switchboard (H.264/VP8 video + SCTP DataChannel) │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. UI Component & React API Design

### 3.1 The Single UI Component: `<EmulatorView />`

`<EmulatorView />` is the only visual UI element provided by the library. It
embeds the WebRTC `<video>` element, manages the canvas layout/aspect ratio, and
attaches an event handler overlay that translates mouse, multi-touch, stylus,
and keyboard events to the emulator:

```tsx
import React, { useRef } from "react";
import { EmulatorView, EmulatorViewRef } from "@android/aquarium";

export function SimpleEmulator() {
  const emulatorRef = useRef<EmulatorViewRef>(null);

  return (
    <div style={{ width: 360, height: 740, background: "#000" }}>
      <EmulatorView
        ref={emulatorRef}
        uri="http://localhost:8080"
        displayId={0}
        onStateChange={(state) => console.log("WebRTC Connection:", state)}
        onError={(err) => console.error("Emulator Error:", err)}
        allowPointerLock={false}
        enableStylus={true}
      />
    </div>
  );
}
```

---

### 3.2 Sensor API & Headless Controls

All environmental controls and emulator services are decoupled from the UI,
available either via framework-agnostic TypeScript objects (`AquariumClient`) or
idiomatic React hooks (`useSensors`, `useLocation`, `useVm`).

#### Example: Interacting with Sensors in React (`useSensors`)

```tsx
import React from "react";
import { EmulatorView, AquariumProvider, useSensors } from "@android/aquarium";

function App() {
  return (
    <AquariumProvider uri="http://localhost:8080">
      <div style={{ display: "flex", gap: "24px" }}>
        {/* The single UI component */}
        <EmulatorView displayId={0} />

        {/* Custom developer-built sensor control panel */}
        <SensorControlPanel />
      </div>
    </AquariumProvider>
  );
}

function SensorControlPanel() {
  const {
    sensors, // Reactive live sensor state { accelerometer, orientationDegrees, lightLux, ... }
    setAccelerometer, // (vec: { x: number; y: number; z: number }) => Promise<void>
    setOrientation, // (rot: { azimuth: number; pitch: number; roll: number }) => Promise<void>
    setAmbientLight, // (lux: number) => Promise<void>
    setProximity, // (distanceCm: number) => Promise<void>
    setTemperature, // (celsius: number) => Promise<void>
  } = useSensors();

  return (
    <div className="panel">
      <h3>Device Sensors & Orientation</h3>

      {/* Orientation controls */}
      <div>
        <label>
          Pitch: {sensors.orientationDegrees?.pitch?.toFixed(1) ?? 0}°
        </label>
        <button
          onClick={() => setOrientation({ azimuth: 0, pitch: 90, roll: 0 })}
        >
          Landscape (90°)
        </button>
        <button
          onClick={() => setOrientation({ azimuth: 0, pitch: 0, roll: 0 })}
        >
          Portrait (0°)
        </button>
      </div>

      {/* Accelerometer */}
      <div>
        <label>Accelerometer (Z-axis gravity):</label>
        <button onClick={() => setAccelerometer({ x: 0, y: 0, z: 9.81 })}>
          Face Up (Z = 9.81 m/s²)
        </button>
        <button onClick={() => setAccelerometer({ x: 0, y: 0, z: -9.81 })}>
          Face Down (Z = -9.81 m/s²)
        </button>
      </div>

      {/* Ambient Light Sensor */}
      <div>
        <label>Ambient Light: {sensors.lightLux ?? 100} Lux</label>
        <input
          type="range"
          min="0"
          max="2000"
          value={sensors.lightLux ?? 100}
          onChange={(e) => setAmbientLight(Number(e.target.value))}
        />
      </div>
    </div>
  );
}
```

---

#### Example: Pure TypeScript / Headless SDK Usage (No React Required)

The sensor API can also be used in plain TypeScript, Vue, Svelte, or automated
test scripts:

```typescript
import { AquariumClient } from "@android/aquarium";

// 1. Initialize client with gRPC-Web proxy endpoint
const client = new AquariumClient({ uri: "http://localhost:8080" });
await client.connect();

// 2. Query current sensor state
const state = await client.sensors.getState();
console.log("Current Accelerometer:", state.accelerometer); // { x: 0, y: 9.81, z: 0 }
console.log("Current Orientation:", state.orientationDegrees);

// 3. Update individual sensors with partial updates (using field masks under the hood)
await client.sensors.setAccelerometer({ x: 0, y: 9.81, z: 0 });
await client.sensors.setOrientation({ azimuth: 0, pitch: 45, roll: 0 });
await client.sensors.setAmbientLight(500); // 500 lux
await client.sensors.setProximity(0); // 0 cm (object touching sensor)
await client.sensors.setTemperature(22.5); // 22.5 °C

// 4. Stream real-time sensor updates from the emulator
const subscription = client.sensors.subscribe((updatedState) => {
  console.log("Live sensor update from guest OS:", updatedState);
});

// Later: unsubscribe when done
subscription.unsubscribe();
```

---

## 4. Subsystems & Capabilities API

### 4.1 Display & Input Subsystem (`InputOverlay`)

Handles browser DOM events and serializes them into high-fidelity
`android.emulation.v2.input.InputEvent` protobuf messages:

| Input Modality    | DOM Event Mapping                                          | Generated v2 Protobuf Event                                                                       | Key Advantage                                                            |
| :---------------- | :--------------------------------------------------------- | :------------------------------------------------------------------------------------------------ | :----------------------------------------------------------------------- |
| **Multi-Touch**   | `pointerdown`, `pointermove`, `pointerup`, `pointercancel` | `PointerEvent` with `tool_type = FINGER`, normalized $(x, y) \in [0.0, 1.0]$, `pointer_id` (0..9) | Automatic multi-finger slot allocation via host `SlotRegistry`.          |
| **Mouse Motion**  | `mousemove`, `pointermove`                                 | `PointerEvent` with `tool_type = MOUSE`, sub-pixel $(x, y)$, relative `delta_x`, `delta_y`        | Supports relative mouse capture in Android Desktop Mode (`pointerlock`). |
| **Mouse Buttons** | `mousedown`, `mouseup`                                     | `PointerEvent` with `button_mask` (`BUTTON_MASK_PRIMARY`, `SECONDARY`, `TERTIARY`)                | Native right-click and middle-click handling.                            |
| **Mouse Wheel**   | `wheel`                                                    | `PointerEvent` with `scroll_x`, `scroll_y`                                                        | Smooth, hardware-scaled vertical and horizontal scrolling.               |
| **Stylus / Pen**  | `pointerdown` / `pointermove` (`pointerType === 'pen'`)    | `PointerEvent` with `tool_type = STYLUS`, `pressure`, `tilt_radians`, `orientation_radians`       | Accurate drawing in Android graphics apps with tilt and pressure.        |
| **Keyboard**      | `keydown`, `keyup`                                         | `KeyEvent` with `dom_code` (e.g. `"KeyA"`, `"Enter"`, `"ArrowUp"`), `action = DOWN / UP`          | Seamless mapping from web DOM `event.code` to Linux evdev keys.          |
| **Text Commit**   | `input`, `compositionend`                                  | `TextEvent` with UTF-8 `text`                                                                     | Fast typing and IME support without keycode loss.                        |

---

### 4.2 WebRTC JSEP Signaling Subsystem (`RtcController`)

Signaling is performed directly over gRPC-Web using `RtcService`:

1. **Session Setup:** Calls `RtcService.RequestRtcStream(requested_tracks)`
   $\to$ receives `RtcSession handle` + `IceServer` list.
2. **Streaming JSEP Stream:** Opens
   `RtcService.ReceiveJsepMessageStream(handle)` to listen for asynchronous SDP
   Answers and ICE candidates.
3. **Offer Submission:** Browser generates `RTCSessionDescriptionInit` Offer and
   sends it via unary `RtcService.SendJsepMessage(handle, offer_json)`.
4. **Data Channels:** Automatically binds the `"input"` SCTP DataChannel to
   `InputSessionChannel` as soon as `dc.onopen` fires.
5. **Media Streams:** Dispatches incoming `MediaStreamTrack` (primary video,
   secondary video, audio) directly to `<video>` elements and Web Audio
   contexts.

---

### 4.3 Discovery & Capabilities Subsystem (`CapabilitiesClient`)

Automatically calls `CapabilitiesService.GetCapabilities()` upon connection to
discover:

- **Displays:** Total active displays, logical widths, heights, DPIs, refresh
  rates, and primary flag.
- **Hardware Features:** Cellular network, GPS, multi-touch support, physical
  keyboard, biometric scanner, hinge sensor.
- **Audio Devices:** Speaker output and microphone capture capability.
- **Codecs:** Supported video decoders (H.264, VP8, VP9, AV1).

---

### 4.4 Environmental & Virtual Device Control

Exposes gRPC-Web convenience clients for:

- **Foldables & Posture (`SystemService` / `SensorsService`):**
  - `setHingeAngle(degrees: number)` (0° = closed, 90° = tabletop, 180° = flat
    open).
  - `setDevicePosture(posture: 'CLOSED' | 'HALF_OPENED' | 'OPENED' | 'FLIPPED')`.
- **Geolocation (`LocationService`):**
  - `setGpsLocation({ latitude, longitude, altitude, speed, heading })`.
- **Virtual Power & Battery (`BatteryService`):**
  - `setBatteryLevel(percentage: number)`,
    `setChargingState('CHARGING' | 'DISCHARGING')`.
- **VM Snapshots & Lifecycle (`VmService`):**
  - `createSnapshot(name: string)`, `restoreSnapshot(name: string)`,
    `pauseVm()`, `resumeVm()`.
- **Android Clipboard (`ClipboardService`):**
  - `setClipboardText(text: string)`, `getClipboardText(): Promise<string>`.

---

## 5. Authentication & Security Architecture

### 5.1 The Protocol Standard (`Authorization: Bearer <JWT>`)

All gRPC-Web communication over HTTP/1.1 requires the standard HTTP
authentication header:

```http
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
```

- **Proxy Forwarding:**
  [`goldfish::grpcweb::GrpcWebServer`](file:///Users/jansene/src/emu-main-next/hardware/generic/goldfish/emulator/tools/grpc-web/ARCHITECTURE.md)
  automatically propagates the `Authorization` header directly into the backend
  `grpc::ClientContext` metadata.
- **Server-Side Validation:** Emulator authentication interceptors validate the
  JWT signature, issuer (`iss`), audience (`aud`), and expiration (`exp`).
  Unauthenticated or expired tokens return `grpc::StatusCode::UNAUTHENTICATED`
  (HTTP 401).

---

### 5.2 Dynamic Token Providers & Auto-Refresh

JWTs in production web environments expire and rotate. The SDK accepts either a
static token string or a dynamic, asynchronous token provider function:

```typescript
export type AuthProvider =
  | string // Static JWT string
  | (() => string | Promise<string>) // Dynamic async token supplier
  | {
      getToken: () => Promise<string>; // Asynchronous token supplier
      onUnauthorized?: (err: Error) => void; // Callback triggered when 401 UNAUTHENTICATED is returned
    };
```

---

### 5.3 gRPC-Web Client Interceptors

The `GrpcWebTransport` layer utilizes client-side interceptors to inject fresh
tokens and handle expiration:

```typescript
class JwtAuthInterceptor {
  constructor(private tokenProvider: () => Promise<string>) {}

  async intercept(request: any, invoker: any) {
    const metadata = request.getMetadata();
    const token = await this.tokenProvider();
    if (token) {
      metadata["Authorization"] = `Bearer ${token}`;
    }
    return invoker(request);
  }
}
```

---

### 5.4 React Usage Examples

#### Static Token (Simple / Local Dev)

```tsx
<AquariumProvider uri="http://localhost:8080" token="eyJhbGciOi...">
  <EmulatorView displayId={0} />
</AquariumProvider>
```

#### Dynamic / Corporate SSO / Firebase / OAuth Provider

```tsx
import { useAuth } from "./auth-context"; // e.g. Firebase, Auth0, Google Identity

function App() {
  const { getIdToken, refreshToken, logout } = useAuth();

  return (
    <AquariumProvider
      uri="http://localhost:8080"
      auth={{
        getToken: async () => await getIdToken(),
        onUnauthorized: () => {
          console.warn("JWT expired, refreshing session...");
          refreshToken().catch(() => logout());
        },
      }}
    >
      <EmulatorView displayId={0} />
    </AquariumProvider>
  );
}
```

---

### 5.5 WebRTC Security Handshake

1. **Signaling Authentication:** The WebRTC session initialization
   (`RtcService.RequestRtcStream` and `ReceiveJsepMessageStream`) is
   authenticated via gRPC-Web with the `Bearer <JWT>`.
2. **Ephemeral Session Isolation:** The host generates a cryptographically
   random GUID (`RtcSession.session_id`) bound to that authenticated participant
   session.
3. **Transport Security:** All real-time media tracks (H.264/VP8) and SCTP
   DataChannels (`"input"`) are encrypted end-to-end using standard
   **DTLS-SRTP**.

---

## 6. Directory Structure & Packaging

```
hardware/generic/goldfish/emulator/ui/
├── BUILD.bazel
└── aquarium/
    ├── ARCHITECTURE.md                  # Component & protocol specification
    ├── DESIGN.md                        # React & TypeScript API design
    ├── BUILD.bazel                      # Bazel build & proto generation
    ├── package.json                     # @android/aquarium npm manifest
    ├── tsconfig.json                    # Strict TypeScript configuration
    ├── vite.config.ts                   # Fast Vite dev server & library packager
    ├── src/
    │   ├── index.ts                     # Public package exports
    │   ├── context/
    │   │   ├── AquariumContext.ts       # React Context definition
    │   │   └── AquariumProvider.tsx     # Context Provider component
    │   ├── hooks/
    │   │   ├── useAquarium.ts           # Global emulator state & actions
    │   │   ├── useDisplay.ts            # Video stream & input overlay hook
    │   │   ├── useHardwareKeys.ts       # Android buttons (Home, Back, Recents)
    │   │   ├── useSensors.ts            # Posture, hinge angle, and orientation
    │   │   └── useSnapshots.ts          # Snapshot creation and restore
    │   ├── components/
    │   │   ├── Aquarium.tsx             # Root namespace export
    │   │   ├── Display.tsx              # Interactive WebRTC video screen
    │   │   ├── ControlBar.tsx           # Standard navigation toolbar
    │   │   ├── MultiDisplayTabs.tsx     # Secondary screen switcher
    │   │   ├── AudioControl.tsx         # Volume & microphone toggle
    │   │   └── SensorsPanel.tsx         # Foldable & sensor controls
    │   ├── core/
    │   │   ├── AquariumClient.ts        # Framework-agnostic TS controller
    │   │   ├── RtcController.ts         # WebRTC PeerConnection & JSEP driver
    │   │   ├── InputChannel.ts          # v2 InputEvent binary serializer
    │   │   └── GrpcWebTransport.ts      # gRPC-Web client factory
    │   └── proto/                       # TypeScript definitions for services_v2
    └── example/
        ├── index.html                   # Interactive browser testing page
        ├── package.json
        ├── vite.config.ts
        └── src/
            ├── App.tsx                  # Showcase demo application
            └── main.tsx
```

---

## 7. Build System Integration (`Bazel` + `Vite`)

1. **Bazel Target:**
   - Generates TypeScript definitions from `@aemu//protos/services_v2/...` via
     standard protoc / ts-proto.
   - Compiles and bundles static production assets into
     `bazel-bin/emulator/ui/aquarium/dist`.
2. **Serving from Emulator / Proxy:**
   - The compiled `dist/` directory can be packaged as a data dependency of
     `goldfish::grpcweb`, allowing `grpc-web-proxy` or `emulator` to host the
     full UI at `http://localhost:8080/`.
3. **NPM Distribution:**
   - Generates standalone ESM and CJS bundles (`dist/index.mjs`,
     `dist/index.js`, `dist/index.d.ts`) published as `@android/aquarium`.

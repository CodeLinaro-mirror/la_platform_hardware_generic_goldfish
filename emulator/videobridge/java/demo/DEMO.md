# Android Emulator WebRTC Java/Kotlin Demo Applications

Interactive GUI demo applications demonstrating high-performance, low-latency
WebRTC video streaming and input event injection to an Android Emulator using
the `webrtc_client_java` library.

______________________________________________________________________

## 1. Overview

The `demo` directory contains two Kotlin desktop applications:

1. **Compose Desktop Demo** (`webrtc_compose_demo` - _Default_): A modern
   desktop application featuring real-time stream telemetry (resolution, frame
   counter, connection status badge), interactive viewport with gesture
   detection, and Android system navigation buttons (`Back`, `Home`, `Recents`).
1. **Swing Demo** (`webrtc_demo`): A lightweight Swing application demonstrating
   frame rendering and touch/mouse/keyboard event forwarding.

______________________________________________________________________

## 2. Quick Start

### Launching via the Automation Script

The easiest way to run the demo is with `launch_java_video_demo.sh`. The script
automatically builds `videobridge`, detects running emulator instances, starts
the gRPC signaling bridge, and launches the demo application.

```bash
# Launch Compose Desktop Demo (Default)
./hardware/generic/goldfish/emulator/videobridge/java/demo/launch_java_video_demo.sh

# Launch Swing Demo
./hardware/generic/goldfish/emulator/videobridge/java/demo/launch_java_video_demo.sh --swing

# Specify custom Video Bridge port
./hardware/generic/goldfish/emulator/videobridge/java/demo/launch_java_video_demo.sh --port 8555

# Specify explicit emulator PID discovery file
./hardware/generic/goldfish/emulator/videobridge/java/demo/launch_java_video_demo.sh --discovery_file ~/Library/Caches/TemporaryItems/avd/running/pid_12345.ini
```

### Building via Bazel

```bash
# Build all demo targets
bazel build @goldfish//emulator/videobridge/java/demo:all

# Run Compose Desktop demo directly
bazel run @goldfish//emulator/videobridge/java/demo:webrtc_compose_demo -- 127.0.0.1 8554

# Run Swing demo directly
bazel run @goldfish//emulator/videobridge/java/demo:webrtc_demo -- 127.0.0.1 8554
```

______________________________________________________________________

## 3. Demo Component Architecture

```
demo/
├── BUILD.bazel                   # Bazel build targets (:webrtc_demo, :webrtc_compose_demo)
├── launch_java_video_demo.sh     # Automation launcher script
└── src/main/kotlin/com/android/emulator/webrtc/demo/
    ├── EmulatorComposeDemoKt.kt   # Main entry point for Compose Desktop demo
    ├── EmulatorDemoKt.kt          # Main entry point for Swing demo
    └── ui/
        ├── EmulatorComposeFrame.kt# Desktop window frame with top header & bottom nav buttons
        ├── EmulatorDemoFrame.kt   # Standard Swing frame
        └── EmulatorVideoPanel.kt  # Custom JPanel rendering BufferedImage & handling events
```

### Key Functional Responsibilities:

- **`EmulatorStreamSession` Integration**: Initializes `WebRtcClient` JNI
  receiver and `VideoBridgeSignalingClient` gRPC signaling, listening for
  connection state transitions and decoded video frames.
- **Coordinate Scaling (`EmulatorVideoPanel`)**: Maps mouse clicks and drags on
  the scaled desktop UI window to target emulator pixel coordinates based on
  image aspect ratio.
- **Keyboard Forwarding (`EmulatorVideoPanel`)**: Captures AWT key events and
  translates them to W3C `KeyboardEvent.key` strings (`"Enter"`, `"Backspace"`,
  `"GoBack"`, `"a"`, etc.), forwarding `keyPressed` (keydown) and `keyReleased`
  (keyup) events over the WebRTC data channel.
- **Navigation Controls (`EmulatorComposeFrame`)**: Offers one-click buttons for
  system navigation (`GoBack`, `GoHome`, `AppSwitch`).

______________________________________________________________________

## 4. Automatic Emulator Discovery

When no `--discovery_file` parameter is provided, `launch_java_video_demo.sh`
automatically searches platform-specific temp directories for active emulator
PID discovery files:

- **macOS**: `~/Library/Caches/TemporaryItems/avd/running/pid_*.ini`
- **Linux**: `/tmp/avd/running/pid_*.ini`
- **Windows**: `%LOCALAPPDATA%\Temp\avd/running/pid_*.ini`

# Component: gRPC Control Services (`services`)

**Role:** gRPC Service Implementations (V1, Incubating, and In-Process WebRTC Wiring)
**Location:** `emulator/plugin/grpc/services/`
**Build Targets:** `@goldfish//emulator/plugin/grpc/services:emulator_controller`, `@goldfish//emulator/plugin/grpc/services:screen_recorder`, `@goldfish//emulator/plugin/grpc/services:in_process_rtc_service`, `@goldfish//emulator/plugin/grpc/services:snapshot_service`, `@goldfish//emulator/plugin/grpc/services:modem_service`, `@goldfish//emulator/plugin/grpc/services:sensor_service_incubating`, `@goldfish//emulator/plugin/grpc/services:vehicle_service`, `@goldfish//emulator/plugin/grpc/services:service_forwarder`, `@goldfish//emulator/plugin/grpc/services:ui_controller_forwarder`

## Overview
Contains the C++ gRPC service implementations registered by `@goldfish//emulator/plugin/grpc:grpc` (`emulator/plugin/grpc/grpc.cc`), spanning the primary `EmulatorController`, incubating services (screen recording, modem, sensors, vehicle), snapshot control, and the in-process WebRTC `Switchboard` / `InputSender` bridge.

## Key Files, Classes & Test Targets
- **Primary EmulatorController (`:emulator_controller`)**:
  - `emulator_controller.cc` & `include/android/emulation/control/emulator_controller.h` (`EmulatorControllerImpl`) — Delegates domain RPCs to `@goldfish//emulator/plugin/grpc/services/emulator_controller_methods`.
- **Screen Recording Service (`:screen_recorder`, test `:screen_recording_impl_test`)**:
  - `screen_recording_impl.cc`, `include/android/emulation/control/incubating/screen_recording_impl.h`, `screen_recording_impl_test.cc`.
  - `android::emulation::control::incubating::ScreenRecordingServiceImpl` (`StartRecording`, `StopRecording`, `ListRecordings`, `ReceiveRecordingEvents`, `FramesEncodedForTesting`) — Subscribes to `goldfish::display::IDisplay` frames, traces `ScreenRecording::GenerateFrame` and `ScreenRecording::FrameGenerated` Perfetto flows, and drives `goldfish::display::VideoRecorder` / `goldfish::display::WebMEncoder` (`@goldfish//emulator/plugin/display:video_recorder`).
- **In-Process WebRTC Service & DataChannel Wiring (`:in_process_rtc_service`, test `:in_process_rtc_service_test`)**:
  - `in_process_rtc_service.cc`, `include/android/emulation/control/in_process_rtc_service.h`, `in_process_rtc_service_test.cc`.
  - `CreateInProcessInputSenderFactory`, `CreateInProcessSwitchboard`, `CreateInProcessRtcServices` — Wires `goldfish::videobridge::Switchboard` (`@goldfish//emulator/videobridge:videobridge_core`) to `goldfish::videobridge::InProcessInputSender` across WebRTC SCTP DataChannels (`DataChannelLabel::kInput`, `DataChannelLabel::kInputV2` $\rightarrow$ `InputSession::DispatchInputEvent`, and `DataChannelLabel::kSensorsV2` $\rightarrow$ `SensorsServiceImpl::ApplySensorsUpdate`), and instantiates both v1 `RtcService` and v2 `goldfish::grpc::v2::RtcServiceImpl` (`@goldfish//emulator/plugin/grpc/services/v2:rtc_service`).
- **Additional Services**:
  - `snapshot_service_impl.cc` / `include/android/emulation/control/snapshot_service_impl.h` (`:snapshot_service`)
  - `modem_service.cc` / `modem_service_test.cc` (`:modem_service`, `:modem_service_test`)
  - `sensor_service_incubating.cc` / `sensor_service_incubating_test.cc` (`:sensor_service_incubating`, `:sensor_service_incubating_test`)
  - `vehicle_service.cc` (`:vehicle_service`)
  - `service_forwarder_impl.cc` / `ui_controller_forwarder.cc` (`:service_forwarder`, `:ui_controller_forwarder`, `:service_forwarder_impl_test`, `:ui_controller_forwarder_test`)

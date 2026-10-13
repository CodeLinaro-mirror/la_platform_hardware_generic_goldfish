# Component: AEMU V2 gRPC Services (`v2`)

**Role:** Strongly-Typed V2 gRPC Service Implementations (`android.emulation.v2.*`)
**Location:** `emulator/plugin/grpc/services/v2/`
**Build Targets:** `@goldfish//emulator/plugin/grpc/services/v2:v2_services`, `@goldfish//emulator/plugin/grpc/services/v2:sensors_service`, `@goldfish//emulator/plugin/grpc/services/v2:sensors_service_test`, `@goldfish//emulator/plugin/grpc/services/v2:rtc_service`, `@goldfish//emulator/plugin/grpc/services/v2:rtc_service_test`, `@goldfish//emulator/plugin/grpc/services/v2:capabilities_service`, `@goldfish//emulator/plugin/grpc/services/v2:capabilities_service_test`, `@goldfish//emulator/plugin/grpc/services/v2:operations_service`, `@goldfish//emulator/plugin/grpc/services/v2:operations_service_test`, `@goldfish//emulator/plugin/grpc/services/v2:hypervisor_service`, `@goldfish//emulator/plugin/grpc/services/v2:hypervisor_service_test`, `@goldfish//emulator/plugin/grpc/services/v2:system_service`, `@goldfish//emulator/plugin/grpc/services/v2:system_service_test`

## Overview
Implements the next-generation `android.emulation.v2` gRPC services defined in `@aemu//protos/services_v2` (`hardware/google/aemu/protos/services_v2/DESIGN.md`) and registers them with the main QEMU gRPC server (`@goldfish//emulator/plugin/grpc:grpc`).

## Key Sub-Components, Files & Symbols (`namespace goldfish::grpc::v2`)
- **Service Registration (`:v2_services`)**:
  - `include/goldfish/grpc/v2/v2_services.h` & `v2_services.cc`:
    - `CreateV2Services(AvdUniverse& avd_universe, VmOperations* vm_operations, ...)` — Instantiates `CapabilitiesServiceImpl`, `SensorsServiceImpl(avd_universe.GetSensorsPhysicalModel())`, `SystemServiceImpl`, and (when `vm_operations` is non-null) `OperationsServiceImpl` and `HypervisorServiceImpl`, called by `CreateServices` in `emulator/plugin/grpc/grpc.cc` with `avd_universe.GetVmOperations()`.
- **V2 System Service (`:system_service`, test `:system_service_test`)**:
  - `system/system_service_impl.h`, `system/system_service_impl.cc`, `system/system_service_impl_test.cc` (`SystemServiceImpl`).
  - Implements `android.emulation.v2.SystemService` (`@aemu//protos/services_v2:system_service_cc_grpc`) over `GuestStatus`, `GrpcNotificationEventSource`, `VmOperations`, `ClipboardChannel`, and `ObservableFingerprintSensor`, covering OS lifecycle & reboot (`GetSystemState`, `StreamSystemState`, `RebootDevice`), guest clipboard synchronization (`GetClipboardState`, `StreamClipboardState`, `UpdateClipboardState`), and biometric fingerprint injection (`InjectFingerprint`).
- **V2 Operations Service (`:operations_service`, test `:operations_service_test`)**:
  - `operations/operations_service_impl.h`, `operations/operations_service_impl.cc`, `operations/operations_service_impl_test.cc` (`OperationsServiceImpl`, `OperationEntry`).
  - Thread-safe AIP-151 `google.longrunning.Operations` registry (`GetOperation`, `ListOperations`, `WaitOperation`, `DeleteOperation`, `CancelOperation`) and programmatic lifecycle methods (`CreateOperation`, `UpdateMetadata`, `CompleteOperation`, `FailOperation`).
- **V2 Hypervisor Service (`:hypervisor_service`, test `:hypervisor_service_test`)**:
  - `hypervisor/hypervisor_service_impl.h`, `hypervisor/hypervisor_service_impl.cc`, `hypervisor/hypervisor_service_impl_test.cc` (`HypervisorServiceImpl`).
  - Implements `android.emulation.v2.HypervisorService` (`@aemu//protos/services_v2:hypervisor_service_cc_grpc`) over `goldfish::vminterface::VmOperations` and `OperationsServiceImpl`, covering VM lifecycle RPCs (`GetVmState`, `StreamVmState`, `PauseVm`, `ResumeVm`, `ResetVm`, `HardResetVm`, `StopVm`) and snapshot management RPCs (`ListSnapshots`, `GetSnapshot`, `UpdateSnapshot`, `CreateSnapshot`, `LoadSnapshot`, `DeleteSnapshot`, `SetQuickbootTarget`, `ExportSnapshot`, `ImportSnapshot`).
- **V2 Sensors Service (`:sensors_service`, test `:sensors_service_test`)**:
  - `environment/sensors_service_impl.h`, `environment/sensors_service_impl.cc`, `environment/sensors_service_impl_test.cc`.
  - `SensorsServiceImpl` (and `SensorsServiceBase`) — Implements `android.emulation.v2.SensorsService` (`GetSensorsState`, `StreamSensorsState`, `UpdateSensorsState`, `StreamSensorsUpdates`, `GetPostureState`, `StreamPostureState`, `UpdatePostureState`, `ApplySensorsUpdate`, `BuildSensorsState`, `BuildPostureState`) over `goldfish::sensors::PhysicalModel` (`@goldfish//emulator/libs/sensors:physical_model` in `emulator/libs/sensors/physical_model.cc`).
  - Also invoked directly in-process by the WebRTC `sensors_v2` DataChannel via `CreateInProcessInputSenderFactory` (`@goldfish//emulator/plugin/grpc/services:in_process_rtc_service`).
- **V2 WebRTC Signaling Service (`:rtc_service`, test `:rtc_service_test`)**:
  - `webrtc/rtc_service_impl.h`, `webrtc/rtc_service_impl.cc`, `webrtc/rtc_service_impl_test.cc`.
  - `RtcServiceImpl` (`SendJsepMessage`, `ReceiveJsepMessageStream`) — Bridges `android.emulation.v2.RtcService` (`@aemu//protos/services_v2:webrtc_service_cc_grpc`) to `goldfish::videobridge::Switchboard` (`@goldfish//emulator/videobridge:videobridge_core`).
  - `ParseSwitchboardJsonToJsep` and `SerializeJsepToSwitchboardJson` — Convert between Switchboard JSON (unwrapping nested `candidate` and `sdp` envelopes) and `android::emulation::v2::JsepMessage` protobufs.
- **V2 Capabilities Discovery Service (`:capabilities_service`, test `:capabilities_service_test`)**:
  - `discovery/capabilities_service_impl.h`, `discovery/capabilities_service_impl.cc`, `discovery/capabilities_service_impl_test.cc` (`CapabilitiesServiceImpl`).

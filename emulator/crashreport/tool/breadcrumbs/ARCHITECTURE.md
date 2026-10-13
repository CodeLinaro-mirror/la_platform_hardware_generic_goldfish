# Component: Crash Report Breadcrumb Processor (`breadcrumbs`)

**Role:** Crashpad Minidump Breadcrumb Metadata Generation & Post-Mortem Decoding
**Location:** `emulator/crashreport/tool/breadcrumbs/`
**Build Targets:** `@goldfish//emulator/crashreport/tool/breadcrumbs:breadcrumbs`, `@goldfish//emulator/crashreport/tool/breadcrumbs:breadcrumb_metadata`, `@goldfish//emulator/crashreport/tool/breadcrumbs:gen_breadcrumb_metadata`, `@goldfish//emulator/crashreport/tool/breadcrumbs:breadcrumb_processor_test`, `@goldfish//emulator/crashreport/tool/breadcrumbs:breadcrumbs_test`, `@goldfish//emulator/crashreport/tool/breadcrumbs:gen_breadcrumb_metadata_test`

## Overview
Decodes compact binary breadcrumb ring buffers (`RawCircularLog` in `@goldfish//emulator/crashreport/tool/breadcrumbs/core`) embedded in minidump crash reports back into human-readable gRPC RPC method calls, ADB-over-vsock packet traces, and device events.

## Key Files & Targets
- **`include/android/crashreport/breadcrumbs/breadcrumb_processor.h`** & **`breadcrumb_processor.cc`** (`:breadcrumbs`):
  - `BreadcrumbProcessor` — Extracts and renders breadcrumb streams from minidumps using `@goldfish//emulator/crashreport/tool/breadcrumbs/core` and `@goldfish//emulator/crashreport/tool/breadcrumbs/renderer`.
- **`gen_breadcrumb_metadata.py`** (`:gen_breadcrumb_metadata`, `:generate_breadcrumb_metadata`, `:breadcrumb_metadata`):
  - Build-time code generator that hashes gRPC service descriptors (`:breadcrumb_service_protos`) into `breadcrumb_metadata.h` and `breadcrumb_metadata.cc`.
- **Producers in Emulator Plugins (PII Gated by `CONFIG_TRACE_GOLDFISH`)**:
  - **gRPC Breadcrumbs (`@goldfish//emulator/plugin/grpc/interceptors:breadcrumb_interceptor`)**: `BreadcrumbInterceptor` (`emulator/plugin/grpc/interceptors/breadcrumb_interceptor.cc`, tested in `:interceptors_test`) records RPC lifecycle metadata and gates raw protobuf payload capture behind `CONFIG_TRACE_GOLDFISH` (`kDefaultCaptureGrpcPayload`).
  - **ADB-Vsock Breadcrumbs (`@goldfish//emulator/plugin/adb-vsock/logger:logger`)**: `AdbMessageLogger`, `AdbLogger`, and `AdbBreadcrumbTracker` (`emulator/plugin/adb-vsock/logger/adb_message_logger.cc` and `adb_breadcrumb_tracker.cc`, tested in `:logger_unittests`) sniff ADB vsock traffic and gate `AdbBreadcrumbTracker` instantiation behind `CONFIG_TRACE_GOLDFISH`.

# Component: Emulator Metrics & Studio Analytics (`metrics`)

**Role:** Core Library / Telemetry & Analytics Reporting
**Location:** `emulator/libs/metrics/`
**Build Targets:** `@goldfish//emulator/libs/metrics:metrics_reporter` (alias `:metrics`), `@goldfish//emulator/libs/metrics:metrics_writer`, `@goldfish//emulator/libs/metrics:fake_metrics_writer`, `@goldfish//emulator/libs/metrics:studio_config`, `@goldfish//emulator/libs/metrics:playstore_metrics_writer`, `@goldfish//emulator/libs/metrics:studio_file_metrics_writer`, `@goldfish//emulator/libs/metrics:text_metrics_writer`, `@goldfish//emulator/libs/metrics:perf_stat_reporter`

## Overview
Implements usage telemetry, performance statistics collection, and Android Studio `analytics.settings` configuration for the emulator using `@studio_stats` Protobuf schemas (`android_studio::AndroidStudioEvent` and `android::analytics::AnalyticsSettings`).

## Key Files, Classes & Targets
- **`include/goldfish/metrics/metrics_reporter.h`** & **`metrics_reporter.cc`** (`:metrics_reporter` / `:metrics`):
  - `goldfish::metrics::MetricsReporter` (`Report`, `SetWriter`, `SetBaseFields`, `Run`) — Thread-safe asynchronous reporter that enriches `AndroidStudioEvent` messages with session UUIDs, emulator version, and host OS metadata before forwarding to a `MetricsWriter`.
  - Tested in `metrics_reporter_test.cc` (`@goldfish//emulator/libs/metrics:metrics_reporter_test`).
- **`include/goldfish/metrics/metrics_writer.h`** (`:metrics_writer`) & **`include/goldfish/metrics/testing/fake_metrics_writer.h`** (`:fake_metrics_writer`):
  - `goldfish::metrics::MetricsWriter` (`Write`) and `goldfish::metrics::MetricsEvent` interface.
  - `goldfish::metrics::testing::FakeMetricsWriter` (`Write`, `WaitForEvents`) — In-memory test double used by unit tests (such as `@goldfish//emulator/telnet:legacy_console_bridge_test` and `:metrics_reporter_test`).
- **`include/goldfish/metrics/studio_config.h`** & **`studio_config.cc`** (`:studio_config`):
  - Namespace `goldfish::metrics::studio`: `EnsureAnalyticsSettings`, `GetAnonymizationSalt`, `GetUserMetricsOptIn`, `GetMetricsUserId`, `GetSpoolDirectory`, `OptInState`.
  - Serializes/deserializes `analytics.settings` via `@studio_stats//:analytics_settings_cc_proto` (`google::protobuf::util::JsonStringToMessage` / `MessageToJsonString`) and implements Android Studio's 28-day GDPR cryptographic salt rotation (`GetCurrentSaltSkew`, `IsSaltRotationNeeded`, `GenerateRandomSalt`).
  - Tested in `studio_config_test.cc` (`@goldfish//emulator/libs/metrics:studio_config_test`).
- **Metrics Writers & Reporters**:
  - `playstore_metrics_writer.cc` / `include/goldfish/metrics/playstore_metrics_writer.h` (`:playstore_metrics_writer`, `:playstore_metrics_writer_test`) — `PlaystoreMetricsWriter` (HTTPS Clearcut publishing via `@curl` with `CURLSSLOPT_NATIVE_CA` and `:gzip_ostream`).
  - `studio_file_metrics_writer.cc` / `include/goldfish/metrics/studio_file_metrics_writer.h` (`:studio_file_metrics_writer`, `:studio_file_metrics_writer_test`) — Spools `.trk` protobuf batch files to disk.
  - `text_metrics_writer.cc` / `include/goldfish/metrics/text_metrics_writer.h` (`:text_metrics_writer`, `:text_metrics_writer_test`).
  - `perf_stat_reporter.cc` / `include/goldfish/metrics/perf_stat_reporter.h` (`:perf_stat_reporter`, `:perf_stat_reporter_test`) & `include/goldfish/metrics/cpu_usage.h` (`:cpu_usage`).
  - `uuid.cc` / `include/goldfish/metrics/uuid.h` (`:uuid`, `:uuid_test`).

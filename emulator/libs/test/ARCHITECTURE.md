# Component: Unit Test Entrypoint & Sanitizer Hooks (`test`)

**Role:** Core Test Infrastructure / GoogleTest Main & Sanitizer Configuration
**Location:** `emulator/libs/test/`
**Build Targets:** `@goldfish//emulator/libs/test:googletest_main`

## Overview
Provides the canonical GoogleTest `main()` entrypoint (`@goldfish//emulator/libs/test:googletest_main`) linked by C++ unit tests across `hardware/generic/goldfish`, along with compile-time sanitizer hooks and leak suppressions so ASAN, LSAN, and TSAN behave deterministically across Linux, macOS, and Windows.

## Key Files & Symbols
- **`gtest_main.cc`** (`@goldfish//emulator/libs/test:googletest_main`):
  - `main(int argc, char* argv[])` — Initializes `absl::InitializeSymbolizer`, `absl::InstallFailureSignalHandler`, `SetupSanitizers()`, `absl::ParseCommandLine`, `absl::InitializeLog`, and `testing::InitGoogleTest` / `RUN_ALL_TESTS()`.
  - `__tsan_default_options()` (`extern "C" const char*`) — Returns default ThreadSanitizer options (`"second_deadlock_stack=1:die_after_fork=0"`) before `main()` starts.
  - `__tsan_default_suppressions()` (`extern "C" const char*`) — Returns default ThreadSanitizer suppressions (including `race:__on_zero_shared_weak` for uninstrumented `libc++` `std::shared_ptr`/`std::weak_ptr` control block teardown and `race:cuttlefish::NetworkService::isRadioOff`).
  - `SetupSanitizers()` — Configures `ASAN_OPTIONS` and `LSAN_OPTIONS` pointing to `leak_suppressions.txt`.
- **`leak_suppressions.txt`**:
  - LeakSanitizer suppression rules bundled as runfile data in `:googletest_main`.
- **`BUILD.bazel`**:
  - Defines `cc_library(name = "googletest_main", linkstatic = True, alwayslink = True)` so the `extern "C"` sanitizer hooks strongly override the sanitizer runtime's weak defaults at link time.

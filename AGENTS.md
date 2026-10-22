# Goldfish (`hardware/generic/goldfish`) — Agent Guide

Architecture overview: [ARCHITECTURE.md](ARCHITECTURE.md). `emu-dev-cli docs search "<topic>"` searches the ARCHITECTURE/DESIGN/AGENTS docs (not source code).

## Subsystem Playbooks

Before exploring source in one of these areas, read its playbook:

| Code | Playbook |
| :--- | :--- |
| `emulator/launcher/`: CLI flags, QEMU argv `Device` builders, AVD/SDK paths | [emulator/launcher/AGENTS.md](emulator/launcher/AGENTS.md) |
| `emulator/plugin/grpc/`: gRPC services and streaming RPCs | [emulator/plugin/grpc/AGENTS.md](emulator/plugin/grpc/AGENTS.md) |
| `emulator/libs/grpc_utils/`: reactor library internals | [emulator/libs/grpc_utils/AGENTS.md](emulator/libs/grpc_utils/AGENTS.md) |
| `emulator/plugin/hal/`: guest↔host HAL devices, adding a HAL | [emulator/plugin/hal/AGENTS.md](emulator/plugin/hal/AGENTS.md) |
| `emulator/plugin/display/`, `emulator/libs/display_pipeline/`: displays, frame capture, screen recording | [emulator/plugin/display/AGENTS.md](emulator/plugin/display/AGENTS.md) |
| `emulator/plugin/vminterface/`, or code calling `VmOperations` / `ScopedVmLock` | [emulator/plugin/vminterface/AGENTS.md](emulator/plugin/vminterface/AGENTS.md) |
| `emulator/videobridge/`: WebRTC streaming, hardware video encoders | [emulator/videobridge/AGENTS.md](emulator/videobridge/AGENTS.md) |
| `emulator/libs/async/`, `emulator/libs/eventing/`: event loops, sockets, `ObservableValue` | [emulator/libs/async/AGENTS.md](emulator/libs/async/AGENTS.md) |
| `emulator/crashreport/`: Crashpad, breadcrumbs, `HangDetector` | [emulator/crashreport/AGENTS.md](emulator/crashreport/AGENTS.md) |

## Build & Test

- Run Bazel from the checkout root (the directory that contains `hardware/`), not from this directory: `hardware/generic/goldfish/MODULE.bazel` is a placeholder. Labels written `//emulator/...` in these docs mean `@goldfish//emulator/...` from the root:
  ```bash
  bazel test --test_output=errors @goldfish//emulator/libs/async:all
  ```
- Sanitizers: add `--config=tsan` or `--config=asan`. clang-tidy: add `--config=tidy`, optionally scoped with `--@goldfish_build//:clang_tidy_check_files=<path substring>`.

## Rules for All Goldfish Code

- **Reuse `//emulator/libs/*`** instead of writing helpers:
  - **Files:** `android::base::file` (`//emulator/libs/file`, `goldfish/file/file.h`) reads, copies, and moves files but has no general writer. Write with `IniFile::Write()`, `CreatePrivateFileExclusive()` (`goldfish/file/file_atomic.h`), or `std::ofstream` on an `fs::path`. Pass `fs::path`, not a narrow `std::string`, to `fopen` / `std::ifstream` / `std::ofstream`: narrow paths handle non-ASCII on Windows only in processes that embed the UTF-8 manifest (Windows 10 1903+).
  - **INI files:** `android::goldfish::IniFile` (`//emulator/libs/ini_file`).
  - **Environment:** `android::base::System::Get()->EnvGet()` / `EnvSet()` (`//emulator/libs/system`), faked by `android::base::TestSystem` (`//emulator/libs/system:test-headers`). Construct it as `TestSystem sys("");`; it reports Windows unless you call `SetOsType()`. The static `System::GetEnvironmentVariable()` / `SetEnvironmentVariable()` use the real process environment and bypass `TestSystem`.
  - **HTTP:** `goldfish::http::WebServer` (`//emulator/libs/http:web_server`) and `goldfish::network::HttpClient` (`//emulator/libs/network:http_client`, created with `CreateCurlHttpClient()`), in tests too.
  - **Event loops, sockets, timers:** `goldfish::async`; see [emulator/libs/async/AGENTS.md](emulator/libs/async/AGENTS.md).
- **Windows UTF-8 manifest (b/572839247):** executables use `cc_binary_with_manifest` (`load("//emulator/tools:cc_binary_with_manifest.bzl", "cc_binary_with_manifest")`). Raw `cc_binary` is only for `linkshared = True` libraries, because the macro always emits an `.exe` manifest. `cc_test` stays raw but must depend on `//emulator/libs/test:googletest_main` (which embeds the same manifest), not `@googletest//:gtest_main`.
- Tests of path handling include a non-ASCII path (e.g. `"测试_🚀_é"`).
- After editing an `AGENTS.md` or `ARCHITECTURE.md`, run `emu-dev-cli docs lint`. It checks relative links, Bazel packages and `:target` names, and that every playbook is listed above and linked from its co-located `ARCHITECTURE.md`. It does **not** check C++ signatures, symbol names, or commands; verify those against the source.

## Concurrency & Lifetime Tests

Any change that touches threads, locks, event-loop posting, callbacks, or object lifetimes needs a test that **fails without the change**: run it red first (or capture the TSAN/ASAN report), then green. A concurrency test that has never failed proves nothing.

| Bug class | How to expose it |
| :--- | :--- |
| Data race | Drive the API from two or more threads released by a `std::atomic<bool>` start barrier, and run with `--config=tsan`. |
| Lock-order inversion | Run both nesting orders (path A takes L1 then L2, path B takes L2 then L1) on the same objects in one test binary; running them sequentially is enough. Non-TSAN debug and fastbuild tests abort on `absl::Mutex` cycles ("Potential Mutex deadlock"); `--config=tsan` reports `lock-order-inversion` for `absl::Mutex` and `std::mutex`. |
| Deadlock via blocking waits (`PostAndWait`, `RemoveCallback` from inside a callback, firing an event while holding a lock the listener takes) | Reproduce the re-entrant path and wait with `absl::Notification::WaitForNotificationWithTimeout`, so the test fails instead of hanging the shard. |
| Use-after-free and teardown | Force the order deterministically (destroy or cancel while a callback is pending, self-deleting fakes, stepping a `TestEventLoop`), and run with `--config=asan`. |
| Wrong-thread calls | Keep `DCHECK(loop->IsOnLoopThread())` in the code; in tests, call through `loop->Post(...)` and `RunAll()`, and use death tests for fatal misuse. |

- Prefer deterministic interleavings (a manually stepped `TestEventLoop`, the fakes listed in each playbook) to sleeps. Add a bounded stress test on top, and shake it with `emu-dev-cli flakiness reproduce --test <label> --target emulator_linux_x64_tsan --iterations 50`.
- Use `absl::Mutex` with `ABSL_GUARDED_BY` / `ABSL_EXCLUSIVE_LOCKS_REQUIRED` / `ABSL_LOCKS_EXCLUDED`: the Linux and macOS toolchains compile with `-Wthread-safety`, and only `absl::Mutex` gets Abseil's lock-order checker.

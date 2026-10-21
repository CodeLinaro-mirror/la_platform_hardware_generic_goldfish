# Emulator Launcher (`//emulator/launcher/...`) — Agent Playbook

**Namespace:** `android::goldfish` (except `goldfish::launcher::fishtank` in `launch_fishtank.h` and `android::goldfish::netsim` in `launch_netsimd.h`; process and loop types are `::goldfish::async`).

Host-side supervisor (`//emulator/launcher:emulator`). It parses CLI flags, resolves AVD/SDK paths and INI configs, binds the telnet console port (the ADB port, console + 1, is only passed to QEMU), and spawns `netsimd` (reusing a running instance), `qemu-system-*`, and the `fishtank` UI (unless `-no-window`) through `::goldfish::async::UvProcessLauncher` (`//emulator/libs/async:libuv_process_launcher`, injected as `LauncherConfig::process_launcher`). `aquarium-ui` and `android-device-hub` are started by the Python harness (`tools/emulator_lib.py`), not by the C++ launcher.

---

## 1. Key Targets & Entrypoints

| Area | Bazel Target | Files | Notes |
| :--- | :--- | :--- | :--- |
| Supervisor | `//emulator/launcher:launcher_lib`, `//emulator/launcher:emulator` | [launcher.h](launcher.h), [launcher.cc](launcher.cc), [main.cc](main.cc) | Event loop, console port, child lifecycle, signal forwarding (first signal: gRPC shutdown, then SIGKILL after 60 s; second signal: SIGKILL). Startup failures are recorded with `fail_startup(absl::Status)`, which keeps the first error and shuts down; `RunLauncher()` returns it. Each caller adds its own context (e.g. "Fatal error whilst launching the emulator: "). |
| CLI flags | `//emulator/launcher/cmdline:cmdline` | [cmdline_options.h](cmdline/include/android/cmdline_options.h), [cmdline_definitions.h](cmdline/include/android/cmdline_definitions.h), [cmdline_option.h](cmdline/include/android/cmdline_option.h) | Legacy C-style option table: declare flags with the `OPT_FLAG` / `OPT_PARAM` X-macros in `cmdline_options.h`; they expand into `AndroidOptions` and are parsed by `android_parse_options()`. |
| AVD & paths | `//emulator/launcher:avd`, `//emulator/launcher:input_paths`, `//emulator/launcher:config_dirs` | [avd.h](include/android/goldfish/avd.h), [input_paths.h](include/android/goldfish/input_paths.h) | SDK/AVD roots, `config.ini` / `hardware-qemu.ini`, `.img` vs. `-qemu.img` resolution. |
| QEMU argv builders | `//emulator/launcher/launch_qemu:launch_qemu`, `//emulator/launcher/launch_qemu:device` | [launch_qemu.h](launch_qemu/launch_qemu.h), [device.h](launch_qemu/device.h), [emulator_config.h](launch_qemu/emulator_config.h) | `android::goldfish::Device` subclasses such as `//emulator/launcher/launch_qemu:adb_device`, `//emulator/launcher/launch_qemu:cpu_device`, `//emulator/launcher/launch_qemu:disk_drive`, `//emulator/launcher/launch_qemu:snapshot_device`, `//emulator/launcher/launch_qemu:wifi_device`. `addDrives()` (`//emulator/launcher/launch_qemu:configure_drives`) adds drive `PciDevice`s. `LaunchQemu::launch_config()` returns the final `absl::StatusOr<::goldfish::async::LaunchConfig>`. |
| netsimd & fishtank | `//emulator/launcher:launch_netsimd`, `//emulator/launcher:launch_fishtank` | [launch_netsimd.h](launch_netsimd.h), [launch_fishtank.h](launch_fishtank.h) | `LaunchConfig`s for the companion processes. |
| Python harness | `//emulator/launcher:emulator_lib`, `//emulator/launcher:launch_emulator` | [emulator_lib.py](tools/emulator_lib.py), [launch_emulator.py](tools/launch_emulator.py), [launcher_test.bzl](tools/launcher_test.bzl) | `launch_and_monitor_emulator()` and `EmulatorRunner`: success is the first log line matching `--target_log_line`, and the `Advertising in discovery file:` line triggers starting `aquarium-ui` / `android-device-hub`. `create_launch_emulator_test` generates `<name>` and `<name>_zip` py_tests tagged `manual` / `exclusive` / `requires-network`, incompatible with Windows. |

---

## 2. Adding or Changing QEMU Arguments

Add a small `Device` (or `PciDevice`) subclass instead of branching in `launch_qemu.cc`:
- Constructors: `explicit Device(std::string id)` (unique id) and `explicit PciDevice(std::string id, std::string addr = "")`.
- `absl::Status initialize(const EmulatorConfig& emulator)` (pure virtual) validates input and computes state. Its errors reach the user through `fail_startup()`, so name the offending flag or path.
- `std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const` emits argv; `absl::flat_hash_map<std::string, std::string> getQemuEnv(const EmulatorConfig& emulator) const` emits environment variables (an empty value unsets the variable).
- Register it with `addDevice<T>(args...)` in `LaunchQemu::addDevices()`. Registration order is initialization and argv order, and a duplicate id is `LOG(QFATAL)`. Keep `-device avdend` the last `-device`: register new QEMU devices before it; only non-device flags (`-L`, `-monitor`, the `-qemu` passthrough) come after it. Use `addDevice<ParameterList>(std::initializer_list<std::string>{...})` for static flags.
- QEMU does not inherit the launcher's environment: it gets only the union of the devices' `getQemuEnv()` maps. To forward a new variable, add it to `kAllowedEnvVars` (`launch_qemu/env_passthrough_device.cc`) or `kPassthroughLoggingAndCrashVars` (`launch_qemu/plugin_init.cc`), or emit it from a device. (netsimd and fishtank inherit the full environment.)
- Wrap any path or name placed inside a QEMU `key=value,...` option with `Device::EscapeQemuOptionValue()` (it doubles commas), and test a path that contains a comma (see the `*EscapesCommas*` tests in [disk_drive_test.cc](launch_qemu/disk_drive_test.cc)).
- Inside `namespace android::goldfish`, spell async types with a leading `::` (`::goldfish::async::LaunchConfig`); `goldfish::async::X` resolves to `android::goldfish::async` and fails to compile.

---

## 3. Environment Reads

The launcher mixes fakeable and real environment reads. `ConfigDirs` uses `System::Get()->EnvGet()` (tests set values with `EnvSet()` on a `TestSystem`), but `ResolveEmulatorPaths` reads `ANDROID_EMULATOR_LAUNCHER_DIR` with the static `System::GetEnvironmentVariable()`, and `main.cc` reads `ANDROID_CLI` and `ANDROID_PRODUCT_OUT` the same way. Tests of those paths must call `SetEnvironmentVariable()`, which changes the real process environment. Use `EnvGet()` in new code.

---

## 4. Hermetic Unit Tests

- **`Device` builders:** use `FakeEmulator` (`//emulator/launcher/launch_qemu:fake_emulator`, `#include "fake_emulator.h"`, visible only inside `launch_qemu`). It owns everything `EmulatorConfig` references; `EmulatorConfig` stores references, so building one from temporaries dangles. Stub reference-returning `MockAvd` methods (`//emulator/launcher:mock_avd`, `#include "mock_avd.h"`) with `ReturnRef`, e.g. `EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));`; only `GetSystemImagePaths()` is pre-stubbed.
- **Non-ASCII paths:** model on `InputPathsTest.ResolveEmulatorPathsWithNonAsciiDirectory` in [input_paths_test.cc](input_paths_test.cc).
- **CLI parsing:** call `android_parse_options(&argc, &argv, &opts)` on a local `AndroidOptions`, like the `parseOptions()` helper in [cmdline_option_test.cc](cmdline/cmdline_option_test.cc); `android_log_parse_warnings()` clears stored obsolete-flag warnings. (`TemporaryCommandLineOptions` is legacy and does not compile here.)

```bash
bazel test --build_tests_only --test_output=errors @goldfish//emulator/launcher/...
```
`--build_tests_only` avoids building `:emulator` and the system-image-dependent `:launch_emulator`. The generated boot tests are tagged `manual`, so they never run as part of `...`.

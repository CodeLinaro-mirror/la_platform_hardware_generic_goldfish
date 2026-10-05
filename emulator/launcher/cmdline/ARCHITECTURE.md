# Component: Command Line Parsing

**Role:** Handles parsing, validation, and storage of command-line arguments for the emulator.
**Location:** `emulator/launcher/cmdline/` **Namespace:** `android::cmdline` (C++),
`android_cmdLine*` (C globals)

## Integration Guide

| Class / Interface / File | Bazel Target | Path | Description |
| :--- | :--- | :--- | :--- |
| `cmdline_options.h` | `@goldfish//emulator/launcher/cmdline:cmdline` | `include/android/cmdline_options.h` | X-Macro table (`CFG_FLAG`, `CFG_PARAM`, `OPT_FLAG`, `OPT_PARAM`) defining all CLI flags (e.g., `-cache`, `-cache_size`, `-cache-size`, `-data`, `-wipe-data`). |
| `AndroidOptions` | `@goldfish//emulator/launcher/cmdline:cmdline` | `include/android/cmdline_definitions.h` | Struct holding all parsed CLI options generated from `cmdline_options.h`. |
| `android_parse_options` | `@goldfish//emulator/launcher/cmdline:cmdline` | `include/android/cmdline_option.h`, `cmdline_option.cc` | Main CLI option parser and validator. Tested in `cmdline_option_test.cc` (`@goldfish//emulator/launcher/cmdline:cmdline_unittests`). |
| `android_help_main` | `@goldfish//emulator/launcher/cmdline:cmdline` | `include/android/help.h`, `help.cc`, `main_help.cc` | CLI help text definitions and `-help-*` output formatting for every flag. |

## Vertical-Slice Guide: Adding or Retiring a C++ CLI Flag & Hardware Property (e.g. `-cache` / `-cache_size`)

When adding, modifying, or retiring a C++ command-line flag and its corresponding AVD hardware property, update these 4 locations across `hardware/generic/goldfish`:
1. **CLI Option Table & Parser (`emulator/launcher/cmdline/`):**
   - `emulator/launcher/cmdline/include/android/cmdline_options.h` — X-Macro option table entry (`OPT_PARAM` / `OPT_FLAG`).
   - `emulator/launcher/cmdline/include/android/cmdline_definitions.h` & `cmdline_option.cc` (`include/android/cmdline_option.h`) — `AndroidOptions` struct & option parsing logic.
   - `emulator/launcher/cmdline/help.cc` (`include/android/help.h`) — User-facing CLI help text.
   - `emulator/launcher/cmdline/cmdline_option_test.cc` — Unit tests (`@goldfish//emulator/launcher/cmdline:cmdline_unittests`).
2. **Hardware Property Schema (`emulator/libs/hardware_config/`):**
   - `emulator/libs/hardware_config/data/hardware-properties.ini` — Canonical INI schema defining hardware properties (e.g., `disk.cachePartition`, `disk.cachePartition.path`, `disk.cachePartition.size`), processed by `emulator/libs/hardware_config/tools/gen-hw-config.py`.
   - `emulator/libs/hardware_config/include/android/goldfish/hardware_config.h`, `hardware_config.cc`, and `test/android/goldfish/fake_hardware_config.h` (`@goldfish//emulator/libs/hardware_config:hardware_config`).
3. **AVD Image & Path Resolution (`emulator/launcher/`):**
   - `emulator/launcher/include/android/goldfish/image_list.h` & `emulator/launcher/avd.cc` (`@goldfish//emulator/launcher:avd`, `@goldfish//emulator/launcher:avd_test`).
4. **QEMU Drive & Device Configuration (`emulator/launcher/launch_qemu/`):**
   - `emulator/launcher/launch_qemu/configure_drives.h` & `configure_drives.cc` (`ConfigureDrives`), `disk_drive.h` & `disk_drive.cc` (`DiskDrive`), and `launch_qemu.cc` (`LaunchQemu`).
   - Tested in `emulator/launcher/launch_qemu/configure_drives_test.cc` (`@goldfish//emulator/launcher/launch_qemu:configure_drives_test`) and `disk_drive_test.cc` (`@goldfish//emulator/launcher/launch_qemu:disk_drive_test`).

## Dependencies

- **System:** `//emulator/libs/system` (OS-specifics).
- **Config:** `//emulator/libs/hardware_config` (Defaults for paths).

## Threading Model

- **Startup Only:** Command line parsing happens once at startup on the main thread. Accessing the
  global `AndroidOptions` after startup is read-only (effectively thread-safe).


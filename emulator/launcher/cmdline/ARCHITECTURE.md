# Component: Command Line Parsing

**Role:** Handles parsing, validation, and storage of command-line arguments for the emulator.
**Location:** `hardware/generic/goldfish/emulator/cmdline`
**Namespace:** `android::cmdline` (C++), `android_cmdLine*` (C globals)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AndroidOptions` | `:cmdline` | `.../cmdline_definitions.h` | Struct holding all parsed options. |
| `android_parse_options` | `:cmdline` | `.../cmdline_option.h` | Main parsing function. |

## Critical Infrastructure
* **X-Macros:** The `AndroidOptions` struct and the parsing logic are generated using X-Macros defined in `android/cmdline_options.h`. This ensures that adding a new option automatically updates the struct definition, help text, and parser.
* **Legacy C Code:** Much of this library is legacy C code (circa 2008) adapted for modern use.
* **Port Parsing:** Includes logic (`android_parse_port_option`) to handle console/adb port assignments and validation.

## Dependencies
* **System:** `//emulator/libs/system` (OS-specifics).
* **Config:** `//emulator/config` (Defaults for paths).

## Threading Model
* **Startup Only:** Command line parsing happens once at startup on the main thread. Accessing the global `AndroidOptions` after startup is read-only (effectively thread-safe).

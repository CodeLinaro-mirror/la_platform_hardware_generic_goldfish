# Component: Emulator Integration Tests

**Role:** Defines comprehensive integration tests for the emulator, including boot tests, CTS (Compatibility Test Suite), and dEQP (Draw Elements Quality Program).
**Location:** `hardware/generic/goldfish/emulator/tests`
**Namespace:** N/A (Build rules and test scripts)

## Integration Guide
| Target | Description |
| :--- | :--- |
| `:boot_test` | Integration test that boots the emulator with a system image and SDK, verifying successful startup. |
| `:release_zip_test` | Verifies the integrity and contents of the emulator release artifact. |

## CTS/ETS and the future

Things are now located in `third_party/adt-infra/goldfish_test`.
e.g. `bazel test @goldfish_test//cts:cts.CtsUsbTests`

## Critical Infrastructure
* **Test Runners:**
    *   `java_test` (`com.android.tools.idea.EmulatorTest`): Used for boot tests, driving the emulator process and inspecting logs/state.
* **Data Dependencies:** Tests depend on full system images (`@android16k-x86_64//:system_image`, etc.) and a mock SDK (`//emulator/sdk`).
* **Environment:** Sets up a controlled environment (`ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`) to ensure reproducibility.

## Dependencies
* **Binaries:** `//emulator/launcher` (The artifact under test).
* **System Images:** External Bazel repositories providing Android system images.
* **SDK:** `//emulator/sdk`.

## Threading Model
* **Test Execution:** Tests typically run as separate processes (Bazel sandboxing). Parallelism is managed by Bazel.

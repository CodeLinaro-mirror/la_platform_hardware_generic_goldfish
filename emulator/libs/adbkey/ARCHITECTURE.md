# Component: ADB Secrets

**Role:** Manages ADB authentication keys (RSA).
**Location:** `hardware/generic/goldfish/emulator/libs/adbkey`
**Namespace:** `goldfish::adb`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AdbKeyResult` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Result of `EnsurePrivateAdbKey` (`private_key_path` and whether the key file was `newly_created` during this call). |
| `GetPrivateAdbKeyPath` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Locates the private key (`adbkey`), copying from `$HOME/.android/adbkey` if needed. |
| `EnsurePrivateAdbKey` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Ensures `adbkey` exists by delegating generation to `adb keygen` atomically under a file lock. |
| `PubkeyFromPrivkey` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Derives the Android ADB public key string (`<base64> <user>@<host>`) from a PEM private key. |
| `ScopedFakeAdbKeygen` | `:test_support` | `include/goldfish/adb/testing/adbkey_test_support.h` | Hermetic test helpers for RSA key generation (`GenerateTestAdbPrivateKey`), stub `adb` binaries, and fake `adb keygen` subprocess execution. |

## Critical Infrastructure
* **Key Discovery:** Searches the configured Android user directory and falls back to copying from `$HOME/.android`.
* **Key Generation:** When `adbkey` is missing, `EnsurePrivateAdbKey` locates the host `adb` binary (`sdk_directory/platform-tools` or `PATH`), acquires a `ScopedFileLock` on `adbkey`, runs `adb keygen` into temporary files, and atomically moves them into place.
* **Format Conversion:** Includes utilities (`internal::AndroidPubkeyEncode`, `PubkeyFromPrivkey`) to convert BoringSSL RSA keys into the binary/base64 format expected by guest `adbd`.

## Dependencies
* **File & Locking:** `//emulator/libs/file`, `//emulator/libs/file:file_lock` (Atomic key replacement and cross-process keygen serialization).
* **Process:** `//emulator/libs/process` (Invoking `adb keygen`).
* **System:** `//emulator/libs/system` (Environment variables and home directory discovery).
* **Crypto:** `@boringssl//:ssl` (RSA key parsing and Android pubkey encoding).

## Threading Model
* **Thread & Process Safe:** `EnsurePrivateAdbKey` serializes concurrent key generation across threads and processes using `ScopedFileLock` and process/sequence-unique temporary paths.

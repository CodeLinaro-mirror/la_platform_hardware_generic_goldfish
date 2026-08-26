# Component: ADB Secrets

**Role:** Manages ADB authentication keys (RSA).
**Location:** `hardware/generic/goldfish/emulator/libs/adbkey`
**Namespace:** `goldfish::adb`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `getPrivateAdbKeyPath` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Locates the private key (`adbkey`). |
| `getPublicAdbKeyPath` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Locates the public key (`adbkey.pub`). |
| `adb_auth_keygen` | `:adbkey` | `include/goldfish/adb/adbkey.h` | Generates a new RSA keypair if missing. |

## Critical Infrastructure
* **Key Discovery:** Searches standard locations (`$HOME/.android`, `$ANDROID_VENDOR_KEYS`) for ADB keys.
* **Key Generation:** If keys are missing, it can generate a new 2048-bit RSA keypair using OpenSSL/BoringSSL.
* **Format Conversion:** Includes utilities (`android_pubkey_encode`) to convert OpenSSL RSA structures into the specific binary format expected by the Android ADB protocol.

## Dependencies
* **System:** `//emulator/libs/system` (To find user and home directories).
* **Crypto:** `@boringssl//:ssl` (RSA operations).

## Threading Model
* **Thread Safe:** Helper functions are stateless or operate on local paths.

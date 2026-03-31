# Component: gRPC Security

**Role:** Implements authentication and authorization for emulator gRPC services.
**Location:** `hardware/generic/goldfish/emulator/grpc/security`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AllowList` | `:security` | `.../allow_list.h` | Manages RPC method access rules (from JSON). |
| `JwtTokenAuth` | `:security` | `.../jwt_token_auth.h` | Verifies JWT tokens using JWKS. |
| `BasicTokenAuth` | `:security` | `.../basic_token_auth.h` | Simple shared-token authentication. |

## Critical Infrastructure
* **Authorization (AllowList):** Categorizes RPC methods into:
    *   **Open:** No authentication required.
    *   **Allowed (Green):** Requires valid token, but no specific audience check.
    *   **Protected (Red):** Requires valid token and the `aud` (audience) claim must match the RPC path.
* **JWT Verification:** Uses the **Google Tink** library for cryptographic verification.
* **Dynamic Keys (JWKS):** `JwkDirectoryObserver` monitors a filesystem directory for changes to `.jwk` files. This enables seamless public key rotation by the host system.

## Dependencies
* **Core:** `//emulator/grpc/async`.
* **External:** `@tink_cc` (Crypto), `@nlohmann_json`, `@re2`.
* **Filesystem:** `//emulator/libs/file_system_watcher`.

## Threading Model
* **Thread Safe:** `JwtTokenAuth` uses a mutex to protect the active `KeysetHandle` during updates from the directory observer. Verification is thread-safe.

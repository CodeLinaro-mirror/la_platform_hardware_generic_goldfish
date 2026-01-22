# Component: Parsing

**Role:** String parsing utilities complementing `absl::strings`.
**Location:** `hardware/generic/goldfish/emulator/libs/parsing`
**Namespace:** `goldfish::parsing`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Split2` | `@goldfish//emulator/libs/parsing` | `.../split2.h` | Splitting strings into exactly two parts. |
| `GetKeyValueStr` | `@goldfish//emulator/libs/parsing` | `.../get_key_value_str.h` | Parsing "key=value" strings. |
| `FromChars` | `@goldfish//emulator/libs/parsing` | `.../from_chars.h` | Wrapper/Helper for C++17 `std::from_chars`. |

## Critical Infrastructure
* **Split2:** Optimized for the common case of splitting a string once (e.g. at the first separator).

## Threading Model
* **Thread Safe:** Stateless string manipulation functions are thread-safe.

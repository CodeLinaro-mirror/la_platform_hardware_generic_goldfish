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
| `ArgStream` | `@goldfish//emulator/libs/parsing` | `.../arg_stream.h` | Type-safe parser for command line arguments. |

## Critical Infrastructure
* **Split2:** Optimized for the common case of splitting a string once (e.g. at the first separator).
* **ArgStream:** Simplifies command logic by providing a stream-oriented interface for consuming arguments with built-in validation and conversion for common types (`int`, `double`, `bool`). Supports quoted and escaped arguments.

## Threading Model
* **Thread Safe:** Stateless string manipulation functions are thread-safe.

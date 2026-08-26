# Component: IniFile

**Role:** Key-value configuration parser and serializer for standard `.ini` files (such as `config.ini`, discovery files, and hardware property files).
**Location:** `hardware/generic/goldfish/emulator/libs/ini_file`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `IniFile` | `:ini_file` | `include/android/goldfish/ini_file.h` | Ordered key-value storage supporting typed queries and serialization. |

## Critical Infrastructure
* **Preserved Insertion Order:** Backed by linked map structure to maintain human-readable ordering when modifying and saving `.ini` files.
* **Type-Safe Accessors:** Provides typed getters/setters (`GetString`, `GetInt`, `GetBool`, `GetDouble`, `GetDiskSize`).
* **Disk Persistence:** `Read(path)` and `Write(path)` handle filesystem I/O safely via `System` and `file` helpers.

## Dependencies
* **Core:** `//emulator/libs/file`, `//emulator/libs/system`.
* **Abseil:** `@abseil-cpp//absl/container:linked_hash_map`, `@abseil-cpp//absl/strings`.

## Threading Model
* **Thread Safe:** Instances are not internally synchronized. Concurrent reads/writes on the same `IniFile` instance require external synchronization.

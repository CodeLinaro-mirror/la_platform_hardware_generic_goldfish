# Component: Archive

**Role:** Provides lightweight serialization interfaces (`IReader`/`IWriter`) with efficient integer encoding.
**Location:** `hardware/generic/goldfish/emulator/libs/archive`
**Namespace:** `goldfish::archive`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `IReader` | `@goldfish//emulator/libs/archive` | `include/goldfish/archive/reader.h` | Abstract interface for reading binary streams. |
| `IWriter` | `@goldfish//emulator/libs/archive` | `include/goldfish/archive/writer.h` | Abstract interface for writing binary streams. |
| `DequeArchive` | `@goldfish//emulator/libs/archive:DequeArchive` | `include/goldfish/archive/deque_archive.h` | In-memory implementation backed by `std::deque`. |
| `Zigzag` | `@goldfish//emulator/libs/archive:zigzag` | `include/goldfish/archive/zigzag/zigzag.h` | Helper for VarInt/ZigZag integer encoding. |

## Critical Infrastructure
| File | Responsibility |
| :--- | :--- |
| `reader.h` | Defines `IReader` and helper functions (`GetUnsigned`, `GetString`) for deserialization. |
| `writer.h` | Defines `IWriter` and operator overloads (`<<`) for serializing primitives using VarInt encoding. |

## Dependencies
* **Internal:** `//emulator/libs/QEMUFile_h` (for QEMU-specific adapters).

## Threading Model
* **Not Thread Safe:** The core interfaces and the `DequeArchive` implementation are **not** thread-safe.
* **Usage:** Serialization operations should typically happen on a single thread or be externally synchronized.

## Logic & Algorithms
### Integer Encoding
To save space, integers are encoded using **VarInt** (Variable-length Integer) and **ZigZag** encoding:
*   **VarInt:** Uses 7 bits per byte, with the MSB acting as a continuation flag.
*   **ZigZag:** Maps signed integers to unsigned integers so that small negative numbers (like -1) are mapped to small unsigned numbers (like 1), making them efficient to compress with VarInt.
    *   `Encode(n) = (n << 1) ^ (n >> 63)`

# Component: Network

**Role:** Utilities for low-level networking, specifically Netlink parsing and scatter-gather I/O.
**Location:** `hardware/generic/goldfish/emulator/libs/network`
**Namespace:** `goldfish::network`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GenericNetlinkMessage` | `@goldfish//emulator/libs/network` | `.../generic_netlink_message.h` | Parser/Builder for Netlink (and Generic Netlink) messages. |
| `IOVector` | `@goldfish//emulator/libs/network` | `.../io_vector.h` | C++ wrapper around `struct iovec` array. |

## Critical Infrastructure
* **IOVector:** Manages a vector of `struct iovec`. Provides methods to copy to/from the virtual contiguous buffer defined by the scatter-gather list.
* **GenericNetlinkMessage:** Encodes/Decodes the standard Netlink header (`nlmsghdr`) and Generic Netlink header (`genlmsghdr`), along with attribute parsing.

## Threading Model
* **Thread Safe:** Classes are pure data containers. Accessing them from multiple threads requires synchronization.

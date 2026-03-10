# Component: Network

**Role:** Utilities for low-level networking, specifically Netlink parsing and scatter-gather I/O. Also IP and DNS resolution utilities.
**Location:** `hardware/generic/goldfish/emulator/libs/network`
**Namespace:** `goldfish::network`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GenericNetlinkMessage` | `@goldfish//emulator/libs/network` | `.../generic_netlink_message.h` | Parser/Builder for Netlink (and Generic Netlink) messages. |
| `IOVector` | `@goldfish//emulator/libs/network` | `.../io_vector.h` | C++ wrapper around `struct iovec` array. |
| `Endpoint` | `:network` | `.../endpoint.h` | A variant type holding an IPv4, IPv6, or Unix Domain Socket address. |
| `DnsResolver` | `:network` | `.../dns_resolver.h` | Asynchronous DNS resolution using `c-ares`. |

## Critical Infrastructure
* **IOVector:** Manages a vector of `struct iovec`. Provides methods to copy to/from the virtual contiguous buffer defined by the scatter-gather list.
* **GenericNetlinkMessage:** Encodes/Decodes the standard Netlink header (`nlmsghdr`) and Generic Netlink header (`genlmsghdr`), along with attribute parsing.
* **Endpoint:** Unifies different socket address types into a single `std::variant`, simplifying API signatures for networking code (like `LibuvSocket`).
* **Conversion:** Helpers to convert between `struct sockaddr`, strings (e.g., "127.0.0.1"), and `Endpoint` objects.

## Dependencies
* **External:** `@c-ares` (DNS).
* **Core:** `@abseil-cpp` (Status, Variant).

## Threading Model
* **Thread Safe:** Classes are pure data containers. Accessing them from multiple threads requires synchronization.
* **Thread Safe:** `Endpoint` and `IpAddress` are immutable data containers. `DnsResolver` is typically used within an event loop context.

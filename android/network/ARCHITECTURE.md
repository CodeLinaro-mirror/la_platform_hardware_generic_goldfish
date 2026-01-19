# Component: Android Network

**Role:** Provides network address abstractions and DNS resolution utilities.
**Location:** `hardware/generic/goldfish/android/network`
**Namespace:** `goldfish::network`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Endpoint` | `:network` | `.../endpoint.h` | A variant type holding an IPv4, IPv6, or Unix Domain Socket address. |
| `DnsResolver` | `:network` | `.../dns_resolver.h` | Asynchronous DNS resolution using `c-ares`. |

## Critical Infrastructure
* **Endpoint:** Unifies different socket address types into a single `std::variant`, simplifying API signatures for networking code (like `LibuvSocket`).
* **Conversion:** Helpers to convert between `struct sockaddr`, strings (e.g., "127.0.0.1"), and `Endpoint` objects.

## Dependencies
* **External:** `@c-ares` (DNS).
* **Core:** `@abseil-cpp` (Status, Variant).

## Threading Model
* **Thread Safe:** `Endpoint` and `IpAddress` are immutable data containers. `DnsResolver` is typically used within an event loop context.

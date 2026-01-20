# Component: <ComponentName>

**Role:** <Single sentence summary of what this package does>
**Location:** `<Directory Path>`
**Namespace:** `<C++ Namespace>`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `<ClassName>` | `<@repo//path:target>` | `<path/to/header.h>` | <Brief usage description> |

## Supported Features (Capabilities)
| Feature | API / Flag | Description |
| :--- | :--- | :--- |
| **<Name>** | `<MethodSignature>` | <What functionality does this unlock?> |
| *Example: TCP Connect* | `Connect(addr)` | *Establishes a non-blocking connection.* |

## Critical Infrastructure
| File | Responsibility |
| :--- | :--- |
| `<MainFile.cpp>` | <Entry point / Main Loop> |
| `<State.h>` | <Global Singleton / Configuration> |

## Dependencies
* **Upstream:** <Key libraries used> (e.g., @aemu//base)
* **Hardware:** <Hardware constraints> (e.g., /dev/kvm, GL Driver)

## Threading Model (Diagram Required)
* **Invariant 1:** <e.g., Methods must be called on loop thread>
* **Invariant 2:** <e.g., Locks held during callbacks>

## Flows & Guides
* [Flow Name](docs/flow_name.md)

## Notes
* <Gotchas, legacy constraints, or performance warnings>
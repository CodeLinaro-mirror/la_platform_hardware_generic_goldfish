# Component: Eventing

**Role:** Provides reactive programming primitives, specifically observable values.
**Location:** `hardware/generic/goldfish/emulator/libs/eventing`
**Namespace:** `goldfish::eventing`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ObservableValue<T, Policy>` | `@goldfish//emulator/libs/eventing` | `include/goldfish/eventing/observable_value.h` | A value that notifies listeners upon change. |

## Critical Infrastructure
* **ObservableValue:** Combines state storage (`T`) with an event source (`CallbackEventSource`).
* **Policies:**
    *   `ObservableValueTriggerAlways`: Fires event on every `SetValue`, even if value is same.
    *   `ObservableValueTriggerOnUpdate`: Fires event only if `old != new`.

## Dependencies
* **Upstream:** `@aemu//base` (Provides `CallbackEventSource`).

## Threading Model
* **Thread Safe:** `SetValue` and `GetValue` are protected by an internal `std::mutex`.
* **Events:** `fireEvent` is called synchronously inside `SetValue` while holding the lock (implied by typical usage, check `aemu` docs if critical).

# Component: Broadcasting

**Role:** A type-safe, thread-safe signal/slot (pub/sub) mechanism.
**Location:** `hardware/generic/goldfish/emulator/libs/broadcasting`
**Namespace:** `goldfish::broadcasting`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Topic<Args...>` | `@goldfish//emulator/libs/broadcasting` | `include/goldfish/broadcasting/broadcasting.h` | The event bus. Publishers broadcast to this object. |
| `Ticket` | `@goldfish//emulator/libs/broadcasting` | `include/goldfish/broadcasting/broadcasting.h` | Handle returned upon subscription. Used to unsubscribe. |

## Critical Infrastructure
* **Subscription Management:** Subscriptions are managed via `Ticket`s. Subscribing returns a `Ticket` which acts as a token.
* **Auto-Unsubscribe:** Callbacks can optionally return a `Ticket` to unsubscribe themselves immediately after execution (useful for one-shot events).

## Dependencies
* **Standard Library:** Uses `<functional>`, `<mutex>`, `<unordered_map>`.

## Threading Model
* **Thread Safe:** `Subscribe`, `Unsubscribe`, and `Broadcast` are protected by an internal `std::mutex`.
* **Blocking:** `Broadcast` executes callbacks synchronously while holding the lock (mostly? No, looks like it holds lock during iteration).
    * **Warning:** Since `Broadcast` holds the lock while invoking callbacks, **callbacks must not call back into the Topic to Subscribe/Unsubscribe**, otherwise a deadlock or iterator invalidation might occur (though the implementation handles erasure via return value carefully, re-entrant modification needs care).

## Logic & Algorithms
### Ticket System
*   `Ticket` holds a `weak_ptr` to the `Topic` and a unique `value_t` ID.
*   The `Topic` maintains a map of `ID -> Callback`.
*   Unsubscribing locks the topic and removes the ID from the map.

# Component: Broadcasting

**Role:** A type-safe, thread-safe signal/slot (pub/sub) mechanism.
**Location:** `hardware/generic/goldfish/emulator/libs/broadcasting`
**Namespace:** `goldfish::broadcasting`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Topic<Args...>` | `@goldfish//emulator/libs/broadcasting` | `include/goldfish/broadcasting/broadcasting.h` | The event bus. Publishers broadcast to this object. |
| `Subscription` | `@goldfish//emulator/libs/broadcasting` | `include/goldfish/broadcasting/broadcasting.h` | Handle returned upon subscription. Used to unsubscribe. |

## Critical Infrastructure
* **Subscription Management:** Subscriptions are managed via `Subscription`s. Subscribing returns a `Subscription` which acts as a token.
* **Auto-Unsubscribe:** A `Subscription` acts as an RAII token and unsubscribes automatically upon destruction. `Topic` also tracks the lifetime of subscribers via `std::weak_ptr`. If a subscriber is destroyed, its subscriptions are skipped during `Broadcast` and automatically cleaned up.

## Dependencies
* **Standard Library:** Uses `<functional>`, `<memory>`, `<utility>`, `<vector>`.
* **Abseil:** Uses `absl::flat_hash_map`, `absl::Mutex`, and `absl/log/check.h`.

## Threading Model
* **Thread Safe:** `Subscribe`, `Unsubscribe`, and state management are protected by an internal `absl::Mutex`.
* **Non-Blocking Broadcast:** To avoid deadlocks, `Broadcast` copies all alive subscriptions into a local vector while holding the lock, then invokes callbacks *without* holding the lock. This allows callbacks to safely subscribe, unsubscribe, or modify the `Topic` re-entrantly.

## Logic & Algorithms
### Subscription System
*   `Subscription` holds a `weak_ptr` to `TopicBase` and a unique `ID` (`uint64_t`).
*   The `Topic` maintains an `absl::flat_hash_map` of `ID -> SubscriptionEntry` (which contains a `weak_ptr` to the subscriber and a trampoline callback function).
*   Unsubscribing locks the topic and removes the ID from the map.
*   During `Broadcast`, `Topic` locks the mutex and iterates through its map to construct a list of active subscriptions. If a subscriber's `weak_ptr` has expired, the entry is lazily erased from the map. The active subscriptions are then invoked sequentially outside of the lock.

# Hang Detection Lifecycle

The `HangDetector` ensures that critical event loops (like the UI thread or IO threads) remain responsive.

## Flow Diagram

```mermaid
sequenceDiagram
    participant App as Emulator Subsystem
    participant Hang as HangDetector (Worker Thread)
    participant Loop as EventLoop (Watched)
    participant Crash as Crashpad

    App->>Hang: addWatchedLooper("IO-Thread", event_loop, 15s)

    loop Heartbeat Cycle
        Hang->>Loop: Post(Heartbeat Task)
        Loop-->>Hang: Heartbeat Task Completed (Sets Timestamp)

        Note over Hang: Sleep 5s...

        Hang->>Hang: Check Timestamp
        alt Timestamp is old (> 15s)
            Hang->>App: Callback(message)
            Hang->>Crash: Generate Crash Dump (Minidump)
            Hang->>Hang: die() (Terminates Process)
        end
    end
```

## Key Components
*   **Heartbeat Task:** A simple lambda posted to the target `EventLoop`. Its execution updates a "last seen" timestamp in the detector.
*   **Worker Thread:** A dedicated thread that wakes up every `hangLoopIterationTimeout` (default 5s) to check all registered loopers.
*   **Thresholds:**
    *   `hangLoopIterationTimeout`: Frequency of the check.
    *   `task_timeout`: Grace period before a looper is declared "hung".

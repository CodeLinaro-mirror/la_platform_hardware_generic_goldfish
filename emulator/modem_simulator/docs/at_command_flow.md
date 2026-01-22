# AT Command Flow

This document details how the Modem Simulator processes AT commands received from the guest's RIL (Radio Interface Layer).

## Flow Diagram

```mermaid
sequenceDiagram
    participant RIL as Guest RIL
    participant Monitor as ChannelMonitor
    participant Modem as ModemSimulator
    participant Parser as CommandParser
    participant Service as ModemService (e.g. SimService)

    RIL->>Monitor: write("AT+CPIN?\r")

    Note over Monitor: Dedicated I/O Thread
    Monitor->>Monitor: Read Command
    Monitor->>Modem: DispatchCommand(Client, "AT+CPIN?")

    Modem->>Parser: Parse("AT+CPIN?")

    loop Service Iteration
        Modem->>Service: HandleCommand(Parser, Client)

        alt Command Match
            Service->>Service: HandleCPIN()
            Service-->>Client: "AT+CPIN: READY"
            Service-->>Client: "OK"
            Note right of Service: Handled = true
        else No Match
            Service-->>Modem: Handled = false
        end
    end

    alt Not Handled
        Modem-->>Client: "ERROR"
    end
```

## Key Components

1.  **ChannelMonitor:** Listens on the socket connected to the guest RIL. When data arrives, it buffers it until a complete command (terminated by `\r` or `\n`) is received.
2.  **Dispatch:** The `ModemSimulator` iterates through all registered services (`SimService`, `NetworkService`, etc.) asking each if it can handle the command.
3.  **CommandParser:** Provides a tokenizer for the service to check the command prefix (e.g., `+CPIN`) and parse arguments.
4.  **Response:** The service writes the response directly back to the `Client` object, which wraps the socket file descriptor.
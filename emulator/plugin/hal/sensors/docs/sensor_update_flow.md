# Sensor Update Flow

This document details how physical sensor data (accelerometer, gyroscope, etc.) propagates from the physics engine to the guest OS.

## Flow Diagram

```mermaid
sequenceDiagram
    participant UI as User Interface / gRPC
    participant Physics as Physics Engine
    participant Model as PhysicalModel
    participant Device as SensorDevice
    participant QEMUD as QEMUD Pipe
    participant Guest as Guest OS (sensors.goldfish)

    Note over UI, Model: Physics Simulation
    UI->>Physics: Update Device Orientation
    Physics->>Model: Poll (Interval)
    Model->>Model: Calculate Sensor Values

    Note over Model, Guest: Data Delivery

    loop Polling Loop (SensorDevice)
        Device->>Model: GetSensor(ID)
        Model-->>Device: {x, y, z}

        Device->>QEMUD: Send(Data Packet)
        QEMUD->>Guest: Pipe Read
    end
```

## Key Components

1.  **Physics Engine:** Calculates the "truth" of the device state (gravity vector, magnetic north) based on user inputs (rotation, location).
2.  **PhysicalModel:** Translates the raw physics state into specific sensor readings (e.g., adding noise, bias, or quantizing to the sensor's resolution).
3.  **SensorDevice:** Runs a polling loop (driven by the guest's requested sampling rate). It queries the `PhysicalModel` and pushes data frames over the QEMUD pipe.

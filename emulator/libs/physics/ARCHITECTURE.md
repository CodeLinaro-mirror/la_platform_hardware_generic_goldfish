# Component: Physics

**Role:** Simulates physical environments and device states for sensor emulation.
**Location:** `hardware/generic/goldfish/emulator/libs/physics`
**Namespace:** `goldfish::physics`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AmbientEnvironment` | `@goldfish//emulator/libs/physics` | `.../ambient_environment.h` | Simulates environmental conditions (Pressure, Temp, Humidity, Light). |
| `InertialModel` | `@goldfish//emulator/libs/physics` | `.../inertial_model.h` | Simulates accelerometer, gyroscope, and magnetometer data. |
| `BodyModel` | `@goldfish//emulator/libs/physics` | `.../body_model.h` | Simulates physiological data (e.g., heart rate). |

## Critical Infrastructure
* **GLM Integration:** Heavily uses the GLM library for vector/matrix math (rotations, quaternions).

## Dependencies
* **External:** `@gfxstream//third_party/glm`.

## Threading Model
* **Thread Safe:** Models are generally data structures. Modification should be synchronized if accessed from multiple threads (e.g., UI updating the model vs Sensors reading it).

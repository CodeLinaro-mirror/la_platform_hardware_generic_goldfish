# Component: Virtual Environment Renderer (VER)

**Role:** Offscreen 3D virtual environment and media frame renderer for Goldfish camera and environment simulation, powered by Goldfish Vulkan (`gvk`).
**Location:** `hardware/generic/goldfish/emulator/libs/ver`
**Namespace:** `android::ver`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ver_create_scene` / `ver_scene_t` | `@goldfish//emulator/libs/ver` | `ver/virtual_environment_renderer.h` | C-compatible API for creating and managing isolated or global virtual scenes. |
| `Scene` | `@goldfish//emulator/libs/ver` | `src/Scene.h` | Manages a 3D scene graph, posters and camera views for different scene modes (meshes, static images, videos). |
| `RendererVulkan` | `@goldfish//emulator/libs/ver` | `src/RendererVulkan.h` | Offscreen Vulkan renderer built on `goldfish::gvk` for meshes, materials, and multi-view framebuffers. |
| `MeshSceneObject` | `@goldfish//emulator/libs/ver` | `src/MeshSceneObject.h` | Loads and renders 3D Wavefront OBJ (`.obj`/`.mtl`) and glTF 2.0 (`.gltf`/`.glb`) assets. |
| `PosterSceneObject` | `@goldfish//emulator/libs/ver` | `src/PosterSceneObject.h` | Renders dynamic 2D poster textures and outlines anchored in 3D space. |

## Critical Infrastructure
* **GVK Offscreen Backend:** Uses `goldfish::gvk` (`SystemMetaLoader`, `InstanceDispatch`, `DeviceDispatch`, `StagingBuffers`, and RAII `DeviceResources`) for Vulkan instance/device initialization, SPIR-V shader pipelines, descriptor sets, and offscreen color/depth framebuffers. All legacy OpenGL ES paths have been removed.
* **Multi-Source Scene Rendering:** Supports 3D virtual rooms with dynamic posters, static image files (`RawImageFileSource`), and FFmpeg-decoded video streams (`RawVideoFileSource`), with `libyuv` format conversion to caller-requested pixel formats.
* **3D Asset Loading:** Integrates `@tinyobjloader` and `@tinygltf` from the Bazel registry for loading geometry, materials, and textures.

## Dependencies
* **External:** `@vulkan-headers`, `@tinyobjloader`, `@tinygltf`, `@gfxstream//third_party/glm`, `@libyuv`, `@libjpeg_turbo//:jpeg`, `@ffmpeg`, `@curl`, `@nlohmann_json//:json`.
* **Internal:** `//emulator/libs/gvk`, `//emulator/libs/imaging`, `//emulator/libs/physics`, `//emulator/libs/sensors:physical_model`, `//emulator/libs/system`, `//emulator/libs/debug`.

## Threading Model
* **Thread Safety:** `ScenesManager` and `Scene` protect scene state, poster updates, and view rendering with internal mutexes (`std::mutex`), allowing camera frame capture (`ver_capture_view`), physical model pose queries, and asynchronous poster/environment updates across threads.

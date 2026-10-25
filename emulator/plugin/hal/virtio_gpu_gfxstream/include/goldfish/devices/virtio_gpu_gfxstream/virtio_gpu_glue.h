// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Host-side access to guest virtio-gpu resources for plugin code that needs
// to read or fill a color buffer outside the virtio command path
// (multi-display read-back, camera gralloc uploads).
//
// The emulator instantiates exactly one of two virtio-gpu devices
// (b/576450291): QEMU's virtio-gpu-rutabaga (renderer reached through
// librutabaga_ffi) or the plugin's virtio-gpu-gfxstream (libgfxstream_backend
// called directly). virtioGpuGlueGetInstance() finds whichever exists through
// QOM and the Image* helpers dispatch to the matching renderer API, so callers
// never need to know which device is in use.
//
// Thread safety: these call straight into the renderer, whose resource table
// is not locked against the device's command processing. Callers must hold
// the BQL (or otherwise know the VM is paused).

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct VirtioGpuGlue;

// The realized virtio-gpu device whose renderer is initialized, or NULL when
// the emulator runs without one (e.g. -gpu off). The pointer stays valid for
// the lifetime of the process and tracks the device: the renderer handle is
// looked up on every call, so a device reset or re-initialization does not
// invalidate it.
struct VirtioGpuGlue* virtioGpuGlueGetInstance(void);

// The QEMU device name behind `instance` ("virtio-gpu-rutabaga" or
// "virtio-gpu-gfxstream"), for logs.
const char* virtioGpuGlueDeviceName(const struct VirtioGpuGlue* instance);

// Uploads a width x height image (stride bytes per row, framebuffer_size
// bytes in total) into resource_id. Returns 0 on success, -1 when `instance`
// is NULL or its renderer is currently not initialized, otherwise the
// renderer's negative errno.
int32_t virtioGpuGlueImageTransfer(struct VirtioGpuGlue* instance, uint32_t resource_id,
                                   uint32_t width, uint32_t height, uint32_t stride,
                                   const void* framebuffer, uint32_t framebuffer_size);

// Reads resource_id back into framebuffer. Same return values as
// virtioGpuGlueImageTransfer().
int32_t virtioGpuGlueImageRead(struct VirtioGpuGlue* instance, uint32_t resource_id, uint32_t width,
                               uint32_t height, uint32_t stride, void* framebuffer,
                               uint32_t framebuffer_size);

#ifdef __cplusplus
}  // extern "C"
#endif

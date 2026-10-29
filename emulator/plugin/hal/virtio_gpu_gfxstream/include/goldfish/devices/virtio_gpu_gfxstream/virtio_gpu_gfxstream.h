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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registers the QOM types "virtio-gpu-gfxstream-device" and
// "virtio-gpu-gfxstream-pci" and the "info virtio-gpu-gfxstream" HMP command.
// Called once from the plugin's GF_REGISTER_TYPES_FUNC.
void virtio_gpu_gfxstream_register_types(void);

// Registers only the PCI transport; called by virtio_gpu_gfxstream_register_types().
void virtio_gpu_gfxstream_pci_register_types(void);

#ifdef __cplusplus
}  // extern "C"
#endif

/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdint.h>
// NOLINTBEGIN
struct rutabaga;

struct rutabaga* rutabagaGetInstance();  // NOLINT

int32_t rutabagaImageTransfer(struct rutabaga* instance, uint32_t resource_id, uint32_t width,
                              uint32_t height, uint32_t stride, const void* framebuffer,
                              uint32_t framebuffer_size);

int32_t rutabagaImageRead(struct rutabaga* instance, uint32_t resource_id, uint32_t width,
                          uint32_t height, uint32_t stride, void* framebuffer,
                          uint32_t framebuffer_size);
// NOLINTEND

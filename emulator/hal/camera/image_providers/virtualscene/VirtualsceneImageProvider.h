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

#include "android/camera/CameraImageProviderAPI.h"

namespace goldfish::camera_image_providers::virtualscene {

struct VirtualsceneImageProvider {
  VirtualsceneImageProvider() = default;

  const char* getId() const;
  int start(const CameraImageProviderStreamConfig* s, unsigned n);
  int capture(const CameraImageProviderCaptureOpts& opts,
              const CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
              const CameraImageProviderStreamCaptureInfo* sci, unsigned scin);
  void stop();

  static void* create(const CameraImageProviderInfo& info);
};

}  // namespace goldfish::camera_image_providers::virtualscene
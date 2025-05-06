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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CameraImageProviderRect {
    uint16_t width;
    uint16_t height;
} CameraImageProviderRect;

struct CameraImageProviderInfoVtbl;

typedef struct CameraImageProviderInfo {
    const struct CameraImageProviderInfoVtbl* vtbl;
    const CameraImageProviderRect* supportedFrameSizes;
    /* implementation defined value used by the `CameraImageProviderInfoFuncs::create` function */
    const void* createArg;
    uint8_t supportedFrameSizesNum;
    uint8_t isBackFacing;
    uint8_t needFreeSupportedSizes;
} CameraImageProviderInfo;

typedef struct CameraImageProviderStreamConfig {
    int32_t id;
    uint32_t format; /* fourCC */
    CameraImageProviderRect size;
} CameraImageProviderStreamConfig;

typedef struct CameraImageProviderWhiteBalance {
    float red;
    float green;
    float blue;
} CameraImageProviderWhiteBalance;

typedef struct CameraImageProviderCaptureOpts {
    CameraImageProviderWhiteBalance whiteBalance;
    float expComp;
} CameraImageProviderCaptureOpts;

typedef struct CameraImageProviderStreamCaptureInfo {
    const CameraImageProviderStreamConfig* cfg;
    const void* bufOpaque; /* sink specific buffer argument */
} CameraImageProviderStreamCaptureInfo;

typedef int (*CameraImageProviderStreamCaptureSink)(void* sinkOpaque,
                                                    const CameraImageProviderStreamCaptureInfo* sci,
                                                    const void* framebuffer,
                                                    size_t framebufferSize);

typedef struct CameraImageProviderVtbl {
    const char* (*getId)(const void* instance);
    int (*start)(void* instance, const CameraImageProviderStreamConfig* s, unsigned n);
    int (*capture)(void* instance, const CameraImageProviderCaptureOpts* opts,
                   CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
                   const CameraImageProviderStreamCaptureInfo* sci, unsigned scin);
    void (*stop)(void* instance);
    /* TODO: saving to a snapshot */
    void (*dctor)(void* instance);
} CameraImageProviderVtbl;

typedef struct CameraImageProviderInfoVtbl {
    void* (*create)(const CameraImageProviderInfo*, const CameraImageProviderVtbl** vtbl);
    void (*createArgDctor)(void* createArg);
    /* TODO: load from a snapshot */
} CameraImageProviderInfoVtbl;

#ifdef __cplusplus
} /* extern "C" */
#endif

/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#ifdef _WIN32
// in /mnt/msvc/win8sdk/win_sdk/include/10.0.17134.0/um/wingdi.h
// windows defines ERROR to 0 and that
// confused the compiler to deduce LOG_ERROR to LOG_0 and fail
#undef ERROR
#endif
#include "absl/log/absl_log.h"
#include "absl/log/check.h"

#define LOG(severity) LOG_##severity
#define LOG_INFO ABSL_LOG(INFO)
#define LOG_WARNING ABSL_LOG(WARNING)
#define LOG_ERROR ABSL_LOG(ERROR)
#define LOG_FATAL ABSL_LOG(FATAL)
#define LOG_DEBUG ABSL_VLOG(1)
#define LOG_VERBOSE ABSL_VLOG(1)

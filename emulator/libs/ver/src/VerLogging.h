/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifdef ERROR
#undef ERROR
#endif

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

// TODO(virtualscene-library): remove old style logging functions and this file
#ifndef dprint
#define dprint(fmt, ...) VLOG(1) << absl::StrFormat(fmt, ##__VA_ARGS__)
#endif
#ifndef dinfo
#define dinfo(fmt, ...) LOG(INFO) << absl::StrFormat(fmt, ##__VA_ARGS__)
#endif
#ifndef dwarning
#define dwarning(fmt, ...) LOG(WARNING) << absl::StrFormat(fmt, ##__VA_ARGS__)
#endif
#ifndef derror
#define derror(fmt, ...) LOG(ERROR) << absl::StrFormat(fmt, ##__VA_ARGS__)
#endif
#ifndef dfatal
#define dfatal(fmt, ...) LOG(FATAL) << absl::StrFormat(fmt, ##__VA_ARGS__)
#endif

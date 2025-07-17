
// Copyright 2025 The Android Open Source Project
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

// TODO: Find a better place for this file

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS
// Maximum number of DNS servers supported by the Android emulator.
#define ANDROID_MAX_DNS_SERVERS 4

#define DEFAULT_IPV4_ADDRESS_AND_PREFIX "10.0.2.15/24"
#define DEFAULT_IPV4_GATEWAY "10.0.2.2"
#define DEFAULT_IPV4_DNS "10.0.2.3"

#define DEFAULT_IPV6_ADDRESS_AND_PREFIX "2001:02::/24"
#define DEFAULT_IPV6_GATEWAY "2001:02::2"
#define DEFAULT_IPV6_DNS "2001:02::3"
__END_DECLS

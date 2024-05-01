// Copyright 2021 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// A minimal set of functions found in unistd.h

#ifndef _MSVC_FCNTL_H
#define  _MSVC_FCNTL_H
#include_next <fcntl.h>


#define win32_path_open android_open_with_mode

#endif
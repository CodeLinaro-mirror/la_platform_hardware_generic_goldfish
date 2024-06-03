// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#ifdef _WIN32
#include <windows.h>

// A set of flags that are only relevant for windows based unit tests
class WindowsFlags {
public:
  static bool sIsParentProcess;
  static HANDLE sChildRead;
  static HANDLE sChildWrite;
  static char sFileLockPath[MAX_PATH];
};
#endif

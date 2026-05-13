#!/usr/bin/env python3
# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import unittest
import os
import tempfile
from gen_breadcrumb_metadata import calculate_crc32, get_cpp_type


class TestGenBreadcrumbMetadata(unittest.TestCase):
    def test_calculate_crc32(self):
        path = "/android.emulation.control.EmulatorController/sendKey"
        # zlib.crc32(b"/android.emulation.control.EmulatorController/sendKey") -> 3192300597
        self.assertEqual(calculate_crc32(path), 3192300597)

    def test_get_cpp_type(self):
        self.assertEqual(
            get_cpp_type(".android.emulation.control.KeyboardEvent"),
            "android::emulation::control::KeyboardEvent",
        )
        self.assertEqual(
            get_cpp_type("google.protobuf.Empty"), "google::protobuf::Empty"
        )


if __name__ == "__main__":
    unittest.main()

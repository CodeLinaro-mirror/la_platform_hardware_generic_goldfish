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
#pragma once
#include <cstdint>
#include <string_view>
namespace android::base::testing {

// A very basic but fast checksum.
class ChecksumCalculator {
  public:
    ChecksumCalculator() : mChecksum(0) {}

    void update(std::string_view data) {
        for (char c : data) {
            mChecksum = (mChecksum << 5) ^ (mChecksum >> 27) ^ static_cast<uint64_t>(c);
        }
    }

    uint64_t getChecksum() const { return mChecksum; }
    void clear() { mChecksum = 0; }

  private:
    uint64_t mChecksum;
};
}  // namespace android::base::testing
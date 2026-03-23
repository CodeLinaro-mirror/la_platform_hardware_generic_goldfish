// Copyright 2026 The Android Open Source Project
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
#include "goldfish/gsm/sms.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "goldfish/parsing/hexbin.h"

namespace goldfish::gsm {
using namespace std::literals::string_view_literals;

TEST(ParseSmsAddress, invalid) {
    EXPECT_FALSE(ParseSmsAddress(""sv).ok());
    EXPECT_FALSE(ParseSmsAddress("01234 56789 01234 56789 0"sv).ok());  // too long
    EXPECT_FALSE(ParseSmsAddress("+012345678901234567890"sv).ok());     // too long
    EXPECT_FALSE(ParseSmsAddress("abcde1abcde1"sv).ok());               // too long
    EXPECT_FALSE(ParseSmsAddress("abc\xF0\x9F\x9A\x80"sv).ok());        // unrepresentable
}

TEST(ParseSmsAddress, numeric) {
    absl::StatusOr<SmsAddress> address;

    address = ParseSmsAddress("123"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 3);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0xF3);

    address = ParseSmsAddress("1234"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 4);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0x43);

    address = ParseSmsAddress("12 34"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 4);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0x43);

    address = ParseSmsAddress("(12) 34"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 4);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0x43);

    address = ParseSmsAddress("12-34"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 4);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0x43);

    address = ParseSmsAddress("+123"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::INTERNATIONAL);
    EXPECT_EQ(address->size, 3);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0xF3);

    address = ParseSmsAddress("+1234"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::INTERNATIONAL);
    EXPECT_EQ(address->size, 4);
    EXPECT_EQ(address->data[0], 0x21);
    EXPECT_EQ(address->data[1], 0x43);

    address = ParseSmsAddress("01234567890123456789"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::DOMESTIC);
    EXPECT_EQ(address->size, 20);
    EXPECT_EQ(address->data[0], 0x10);
    EXPECT_EQ(address->data[1], 0x32);
    EXPECT_EQ(address->data[8], 0x76);
    EXPECT_EQ(address->data[9], 0x98);

    address = ParseSmsAddress("+01234567890123456789"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::INTERNATIONAL);
    EXPECT_EQ(address->size, 20);
    EXPECT_EQ(address->data[0], 0x10);
    EXPECT_EQ(address->data[1], 0x32);
    EXPECT_EQ(address->data[8], 0x76);
    EXPECT_EQ(address->data[9], 0x98);
}

TEST(ParseSmsAddress, alphabetic) {
    absl::StatusOr<SmsAddress> address;

    address = ParseSmsAddress("+"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::ALPHANUMERIC);
    EXPECT_EQ(address->size, (1 * 7 + 4 - 1) / 4);

    address = ParseSmsAddress("abc"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::ALPHANUMERIC);
    EXPECT_EQ(address->size, (3 * 7 + 4 - 1) / 4);

    address = ParseSmsAddress("abcdea"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::ALPHANUMERIC);
    EXPECT_EQ(address->size, (6 * 7 + 4 - 1) / 4);

    address = ParseSmsAddress("abcdeabcde"sv);
    ASSERT_TRUE(address.ok());
    EXPECT_EQ(address->toa, SmsAddress::TOA::ALPHANUMERIC);
    EXPECT_EQ(address->size, (10 * 7 + 4 - 1) / 4);
}

TEST(SmsPdusFromUtf8, empty_message) {
    const absl::StatusOr<std::vector<SmsPdu>> pdus = SmsPdusFromUtf8("(123) 555-0987"sv, ""sv);

    ASSERT_TRUE(pdus.ok());
    ASSERT_EQ(pdus->size(), 1);
    EXPECT_EQ(parsing::BinToHex((*pdus)[0].data), "0001000a812153559078000000"sv);
}

TEST(SmsPdusFromUtf8, one_pdu) {
    const absl::StatusOr<std::vector<SmsPdu>> pdus =
            SmsPdusFromUtf8("(123) 555-0987"sv, "This is a short text message"sv);

    ASSERT_TRUE(pdus.ok());
    ASSERT_EQ(pdus->size(), 1);
    EXPECT_EQ(parsing::BinToHex((*pdus)[0].data),
              "0001000a81215355907800001c54747a0e4acf4161d01cfd96d341f4329e0e6a97e7f3f0b90c"sv);
}

TEST(SmsPdusFromUtf8, many_pdus) {
    const std::string_view kLongMessage =
            "Copyright 2026 The Android Open Source Project. "
            "Licensed under the Apache License, Version 2.0 (the \"License\"); "
            "you may not use this file except in compliance with the License."sv;

    const absl::StatusOr<std::vector<SmsPdu>> pdus =
            SmsPdusFromUtf8("(123) 555-0987"sv, kLongMessage);

    ASSERT_TRUE(pdus.ok());
    ASSERT_EQ(pdus->size(), 2);

    EXPECT_EQ(parsing::BinToHex((*pdus)[0].data),
              "0041000a8121535590780000a0050003000201866f785e9e3ea3e920194c660351d16550d04d96bfd"
              "364d0135e7683a6efba7c5c0641e56f75794c778198e971d93d2f93417537b92c07d1d16550101e1e"
              "a3cb20667a5c76cfcb2c90b52c9fa7df6e90cc0503a1e8e83248c44c8fcbee795994da81f2ef3aa81"
              "dce83dc6f3aa83e2f83e8e8f41c644eb3cba0327e5c86d341693768fc6ec3d9"sv);

    EXPECT_EQ(parsing::BinToHex((*pdus)[1].data),
              "0041000a81215355907800001e050003000202d261f7b80cbaa7e968101d5d0631d3e3b27b5e7601"sv);
}

TEST(SmsPdusFromBinary, basic) {
    const std::vector<uint8_t> input = {0x01, 0x23, 0x45, 0x56, 0x78};
    const absl::StatusOr<std::vector<SmsPdu>> pdus = SmsPdusFromBinary(input);

    ASSERT_TRUE(pdus.ok());
    ASSERT_EQ(pdus->size(), 1);
    EXPECT_EQ((*pdus)[0].data, input);
}

TEST(CalculateGsm7NumPdus, basic) {
    EXPECT_EQ(CalculateGsm7NumPdus(""sv, 3), 0);
    EXPECT_EQ(CalculateGsm7NumPdus("a"sv, 3), 1);
    EXPECT_EQ(CalculateGsm7NumPdus("ab"sv, 3), 1);
    EXPECT_EQ(CalculateGsm7NumPdus("abc"sv, 3), 1);
    EXPECT_EQ(CalculateGsm7NumPdus("abcd"sv, 3), 2);
    EXPECT_EQ(CalculateGsm7NumPdus("abcde"sv, 3), 2);
    EXPECT_EQ(CalculateGsm7NumPdus("abcdef"sv, 3), 2);
    EXPECT_EQ(CalculateGsm7NumPdus("abcdefg"sv, 3), 3);

    // '~' occupies two septets: 'ab', '~c', 'd'
    EXPECT_EQ(CalculateGsm7NumPdus("ab~cd"sv, 3), 3);
}

}  // namespace goldfish::gsm

// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "goldfish/devices/boot/boot_properties_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "android/status/status_matcher_macros.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/boot/boot_property_string.h"
#include "goldfish/devices/qemud/qemud.h"
#include "goldfish/devices/test_connector_registry.h"

namespace goldfish::devices::boot {

using goldfish::async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

using namespace std::string_literals;

TEST(LimitedStringTest, DefaultConstructor) {
    LimitedString<10> str;
    EXPECT_EQ(str.Get(), ""s);
}

TEST(LimitedStringTest, ConstructorWithString) {
    auto str = LimitedString<10>::Create("hello");
    ASSERT_OK(str);
    EXPECT_EQ(str->Get(), "hello"s);
}

TEST(LimitedStringTest, SetString) {
    LimitedString<10> str;
    EXPECT_OK(str.Set("world"));
    EXPECT_EQ(str.Get(), "world"s);
}

TEST(LimitedStringTest, ConversionToString) {
    auto str = LimitedString<10>::Create("test");
    ASSERT_OK(str);
    std::string str2 = *str;
    EXPECT_EQ(str2, "test"s);
}

TEST(LimitedStringTest, LengthLimit) {
    EXPECT_THAT(LimitedString<5>::Create("abcdef"),
                absl_testing::StatusIs(absl::StatusCode::kOutOfRange,
                                       testing::HasSubstr("String exceeds maximum length")));
}

TEST(LimitedStringTest, EqualityOperator) {
    ASSERT_OK_AND_ASSIGN(auto str1, LimitedString<10>::Create("hello"));
    ASSERT_OK_AND_ASSIGN(auto str2, LimitedString<10>::Create("hello"));
    ASSERT_OK_AND_ASSIGN(auto str3, LimitedString<10>::Create("world"));

    EXPECT_EQ(str1, str2);
    EXPECT_NE(str1, str3);
}

TEST(BootPropertyStringTest, ValidCharacters) {
    BootPropertyString<10> str;
    EXPECT_OK(str.Set("valid_str"));
    EXPECT_EQ(str.Get(), "valid_str"s);
}

TEST(BootPropertyStringTest, InvalidCharacters) {
    EXPECT_THAT(BootPropertyString<10>::Create("invalid "),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: ' '")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid="),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '='")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid$"),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '$'")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid*"),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '*'")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid?"),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '?'")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid'"),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '''")));
    EXPECT_THAT(BootPropertyString<10>::Create("invalid\""),
                absl_testing::StatusIs(
                        absl::StatusCode::kInvalidArgument,
                        testing::HasSubstr("Property name contains invalid character: '\"'")));
}

TEST(BootPropertyStringTest, LengthLimit) {
    EXPECT_THAT(BootPropertyString<5>::Create("abcdef"),
                absl_testing::StatusIs(absl::StatusCode::kOutOfRange,
                                       testing::HasSubstr("String exceeds maximum length")));
    // EXPECT_THROW({ BootPropertyString<5> str("abcdef"); }, std::length_error);
}

class BootPropertiesDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::Create();
        mQemuLoop = TestEventLoop::Create();

        IBootPropertiesDevice::Properties props;
        registerWithProps(props);
    }

  public:
    void registerWithProps(IBootPropertiesDevice::Properties props) {
        IBootPropertiesDevice::RegisterDevice(&registry, props, mClientLoop.get(), mQemuLoop.get());
        device = registry.ConstructHalDevice<IBootPropertiesDevice>();
        test_socket = registry.HalSocket();
        clear();
        device->OnConnect();
    }
    void receive(std::string_view msg) { device->OnReceive(qemud::EncodeQemudPacket(msg)); }
    void clear() { test_socket->storage.clear(); }

  protected:
    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;

    TestConnectorRegistry registry;
    TestHalSocket* test_socket;
    IBootPropertiesDevice::Properties props;
    IBootPropertiesDevice* device;
};

TEST_F(BootPropertiesDeviceTest, sendsBootProperties) {
    ASSERT_OK_AND_ASSIGN(auto props2, IBootPropertiesDevice::MakeProperties({{"foo", "bar"}}));
    registerWithProps(props2);
    receive("list");
    EXPECT_EQ(test_socket->storage, "0007foo=bar0000");
}

}  // namespace goldfish::devices::boot

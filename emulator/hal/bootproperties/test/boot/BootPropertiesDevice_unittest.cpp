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
#include "android/boot/BootPropertiesDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "android/base/testing/TestSystem.h"
#include "android/goldfish/config/fake-avd.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/qemud.h"
#include "goldfish/devices/test_connector_registry.h"

namespace {

typedef void QEMUResetHandler(void* opaque);

static QEMUResetHandler* sResetHandler;
static void* sOpaque;
extern "C" {
void qemu_register_reset(QEMUResetHandler* func, void* opaque) {
    sResetHandler = func;
    sOpaque = opaque;
}
}
}  // namespace

namespace goldfish::devices::boot {

using android::base::TestSystem;
using goldfish::async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;

using namespace std::string_literals;

TEST(LimitedStringTest, DefaultConstructor) {
    LimitedString<10> str;
    EXPECT_EQ(str.get(), ""s);
}

TEST(LimitedStringTest, ConstructorWithString) {
    LimitedString<10> str("hello");
    EXPECT_EQ(str.get(), "hello"s);
}

TEST(LimitedStringTest, SetString) {
    LimitedString<10> str;
    str.set("world");
    EXPECT_EQ(str.get(), "world"s);
}

TEST(LimitedStringTest, AssignmentOperator) {
    LimitedString<10> str;
    str = "foobar";
    EXPECT_EQ(str.get(), "foobar"s);
}

TEST(LimitedStringTest, ConversionToString) {
    LimitedString<10> str("test");
    std::string str2 = str;
    EXPECT_EQ(str2, "test"s);
}

TEST(LimitedStringTest, LengthLimit) {
    EXPECT_THROW({ LimitedString<5> str("abcdef"); }, std::length_error);
}

TEST(LimitedStringTest, EqualityOperator) {
    LimitedString<10> str1("hello");
    LimitedString<10> str2("hello");
    LimitedString<10> str3("world");

    EXPECT_EQ(str1, str2);
    EXPECT_NE(str1, str3);
}

TEST(BootPropertyStringTest, ValidCharacters) {
    BootPropertyString<10> str;
    str.set("valid_str");
    EXPECT_EQ(str.get(), "valid_str"s);
}

TEST(BootPropertyStringTest, InvalidCharacters) {
    EXPECT_THROW({ BootPropertyString<10> str("invalid "); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid="); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid$"); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid*"); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid?"); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid'"); }, InvalidPropertyName);
    EXPECT_THROW({ BootPropertyString<10> str("invalid\""); }, InvalidPropertyName);
}

TEST(BootPropertyStringTest, LengthLimit) {
    EXPECT_THROW({ BootPropertyString<5> str("abcdef"); }, std::length_error);
}

class BootPropertiesDeviceTest : public ::testing::Test {
    void SetUp() override {
        mClientLoop = TestEventLoop::create();
        mQemuLoop = TestEventLoop::create();

        IBootPropertiesDevice::Properties props;
        registerWithProps(props);
    }

  public:
    void registerWithProps(IBootPropertiesDevice::Properties props) {
        IBootPropertiesDevice::registerDevice(&registry, props, mClientLoop.get(), mQemuLoop.get());
        device = registry.constructHalDevice<IBootPropertiesDevice>();
        test_socket = registry.halSocket();
        clear();
        device->onConnect();
    }
    void receive(std::string_view msg) { device->onReceive(qemud::encodeQemudPacket(msg)); }
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
    props["foo"_bps] = "bar";
    registerWithProps(props);
    receive("list");
    EXPECT_EQ(test_socket->storage, "0007foo=bar0000");
}

}  // namespace goldfish::devices::boot
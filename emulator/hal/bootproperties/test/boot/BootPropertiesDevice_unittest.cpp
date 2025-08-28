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

#include <android/base/testing/TestSystem.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/goldfish/config/fake-avd.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/devices/test_socket.h"

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

TEST(BootPropertiesDeviceTest, canCreateDevice) {
    IBootPropertiesDevice::Properties props;
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, props, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();
    EXPECT_NE(device, nullptr);
}

TEST(BootPropertiesDeviceTest, mountsDataPartition) {
    IBootPropertiesDevice::Properties props;
    props["foo"_bps] = "bar";
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, props, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();
    device->onReceive("list", 4);
    EXPECT_TRUE(device->isDataPartitionMounted());
}

TEST(BootPropertiesDeviceTest, sendsBootProperties) {
    IBootPropertiesDevice::Properties props;
    props["foo"_bps] = "bar";
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, props, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();
    auto test_socket = registry.getSocket();
    device->onReceive("list", 4);
    EXPECT_EQ(test_socket->storage, "0007foo=bar");
}

TEST(BootPropertiesDeviceTest, receivesMountEvent) {
    BootPropertyStatus received;
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, {}, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();
    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](BootPropertyStatus event) { received = event; });
    device->onReceive("list", 4);
    EXPECT_THAT(received.dataPartitionMounted, Eq(true));
}

TEST(BootPropertiesDeviceTest, registersResetHandler) {
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, {}, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();

    EXPECT_NE(sResetHandler, nullptr);
    EXPECT_EQ(sOpaque, device);
}

TEST(BootPropertiesDeviceTest, resetHandlerResetsBootCompleted) {
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, {}, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();
    device->onReceive("list", 4);

    // Simulate a reset
    sResetHandler(sOpaque);
    EXPECT_THAT(device->isDataPartitionMounted(), Eq(false));
}

TEST(BootPropertiesDeviceTest, firesResetEvent) {
    BootPropertyStatus received;
    TestConnectorRegistry registry;
    IBootPropertiesDevice::registerDevice(&registry, {}, qemu_register_reset);
    auto device = registry.constructDevice<IBootPropertiesDevice>();

    auto scoped = android::base::eventing::makeScopedCallback(
            *device, [&received](BootPropertyStatus event) { received = event; });

    device->onReceive("list", 4);
    EXPECT_THAT(received.dataPartitionMounted, Eq(true));

    // Simulate a reset
    sResetHandler(sOpaque);
    EXPECT_THAT(received.dataPartitionMounted, Eq(false));
}

}  // namespace goldfish::devices::boot
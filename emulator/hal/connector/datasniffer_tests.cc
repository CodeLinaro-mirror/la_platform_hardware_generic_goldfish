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

#include <gtest/gtest.h>

#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {
using cable::IDataSniffer;
using cable::IPlug;
using cable::ISocket;
using cable::PlugPtr;
using cable::SocketPtr;
using namespace std::literals;

namespace {
struct TestSocket : public ISocket {
    void SendAsync(const void* data, size_t size) override { dataSniffer->ToSocket(data, size); }

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) {
        dataSniffer->ToPlug(data.data(), data.size());
        return plug->OnReceive(data.data(), data.size());
    }

    void SetDataSniffer(std::unique_ptr<IDataSniffer> sniffer) override {
        dataSniffer = std::move(sniffer);
    }

    PlugPtr plug;
    std::unique_ptr<IDataSniffer> dataSniffer;
};

struct TestPlug : public IPlug {
    TestPlug(SocketPtr s) : socket(std::move(s)) {}

    cable::SocketPtr OnUnplug() override { return std::move(socket); }

    bool OnReceive(const void* data, size_t size) override {
        const std::string_view msg = "E-I-E-I-O"sv;
        socket->SendAsync(msg.data(), msg.size());
        return true;
    }

    SocketPtr socket;
};

struct TestSniffer : public IDataSniffer {
    void ToSocket(const void* data, size_t dataSize) override {
        toSocketMsg.assign(static_cast<const char*>(data), dataSize);
    }

    void ToPlug(const void* data, size_t dataSize) override {
        toPlugMsg.assign(static_cast<const char*>(data), dataSize);
    }

    std::string toSocketMsg;
    std::string toPlugMsg;
};

}  // namespace

TEST(DataSniffer, basic) {
    auto sniffer = std::make_unique<TestSniffer>();
    TestSniffer* snifferWeakPtr = sniffer.get();

    TestSocket socket;
    socket.SetDataSniffer(std::move(sniffer));
    socket.plug = std::make_shared<TestPlug>(SocketPtr(&socket));

    socket.send("Old MacDonald had a farm"sv);

    EXPECT_EQ(snifferWeakPtr->toPlugMsg, "Old MacDonald had a farm");
    EXPECT_EQ(snifferWeakPtr->toSocketMsg, "E-I-E-I-O");

    socket.plug->OnUnplug();
}

}  // namespace devices
}  // namespace goldfish

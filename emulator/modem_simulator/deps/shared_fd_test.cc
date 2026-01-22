/* Copyright (C) 2025 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "common/libs/fs/shared_fd.h"

#include <gtest/gtest.h>

#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

TEST(SharedFDTest, Set_IsSet) {
    SharedFD a;
    SharedFD b;
    ASSERT_TRUE(SharedFD::Pipe(&a, &b));

    int max_index_a = 0;
    fd_set fds_a;
    FD_ZERO(&fds_a);

    EXPECT_FALSE(a->IsSet(&fds_a));
    a->Set(&fds_a, &max_index_a);
    EXPECT_GT(max_index_a, 0);
    EXPECT_TRUE(a->IsSet(&fds_a));

    int max_index_b = 0;
    fd_set fds_b;
    FD_ZERO(&fds_b);

    EXPECT_FALSE(b->IsSet(&fds_b));
    b->Set(&fds_b, &max_index_b);
    EXPECT_GT(max_index_b, 0);
    EXPECT_TRUE(b->IsSet(&fds_b));

    EXPECT_FALSE(a->IsSet(&fds_b));
    EXPECT_FALSE(b->IsSet(&fds_a));
    EXPECT_NE(max_index_a, max_index_b);
}

TEST(SharedFDTest, MarkAll) {
    SharedFD a;
    SharedFD b;
    ASSERT_TRUE(SharedFD::Pipe(&a, &b));

    SharedFDSet sfdset;
    sfdset.Set(a);
    sfdset.Set(b);

    int max_index = 0;
    fd_set fds;
    FD_ZERO(&fds);

    impl::MarkAll(sfdset, &fds, &max_index);
    EXPECT_GT(max_index, 0);
    EXPECT_TRUE(a->IsSet(&fds));
    EXPECT_TRUE(b->IsSet(&fds));
}

TEST(SharedFDTest, CheckMarked_empty) {
    SharedFD a;
    SharedFD b;
    ASSERT_TRUE(SharedFD::Pipe(&a, &b));

    SharedFDSet sfdset;
    sfdset.Set(a);
    ASSERT_TRUE(sfdset.IsSet(a));
    sfdset.Set(b);
    ASSERT_TRUE(sfdset.IsSet(b));

    fd_set fds;
    FD_ZERO(&fds);
    impl::CheckMarked(&fds, &sfdset);

    EXPECT_FALSE(sfdset.IsSet(a));
    EXPECT_FALSE(sfdset.IsSet(b));
}

TEST(SharedFDTest, CheckMarked_some) {
    SharedFD a;
    SharedFD b;
    ASSERT_TRUE(SharedFD::Pipe(&a, &b));

    SharedFDSet sfdset;
    sfdset.Set(a);
    ASSERT_TRUE(sfdset.IsSet(a));
    sfdset.Set(b);
    ASSERT_TRUE(sfdset.IsSet(b));

    int max_index = 0;
    fd_set fds;
    FD_ZERO(&fds);

    b->Set(&fds, &max_index);
    EXPECT_GT(max_index, 0);

    impl::CheckMarked(&fds, &sfdset);

    EXPECT_FALSE(sfdset.IsSet(a));
    EXPECT_TRUE(sfdset.IsSet(b));
}

TEST(SharedFDTest, pipe) {
    SharedFD consumer;
    SharedFD producer;
    ASSERT_TRUE(SharedFD::Pipe(&consumer, &producer));
    ASSERT_TRUE(consumer);
    ASSERT_TRUE(producer);

    ASSERT_EQ(producer->Write("test", 4), 4);

    char data[4];
    ASSERT_EQ(consumer->Read(data, 4), 4);
    EXPECT_EQ(::memcmp(data, "test", 4), 0);
}

TEST(SharedFDTest, socketpair) {
    SharedFD a;
    SharedFD b;
    ASSERT_TRUE(SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0, &a, &b));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);

    ASSERT_EQ(a->Write("test", 4), 4);

    char data[4];
    ASSERT_EQ(b->Read(data, 4), 4);
    EXPECT_EQ(::memcmp(data, "test", 4), 0);
}

TEST(SharedFDTest, server_client) {
    const SharedFD server = SharedFD::SocketLocalServer();
    ASSERT_TRUE(server);

    const SharedFD client = SharedFD::SocketClient(server);
    ASSERT_TRUE(client);

    const SharedFD conn = SharedFD::Accept(*server);
    ASSERT_TRUE(conn);

    ASSERT_EQ(client->Write("test", 4), 4);

    char data[4];
    ASSERT_EQ(conn->Read(data, 4), 4);
    EXPECT_EQ(::memcmp(data, "test", 4), 0);
}

TEST(SharedFDTest, server_client_with_endpoint) {
    const SharedFD server = SharedFD::SocketLocalServer();
    ASSERT_TRUE(server);

    struct sockaddr_storage addr;
    socklen_t addrlen = 0;
    ASSERT_TRUE(server->Endpoint(&addr, &addrlen));
    ASSERT_GT(addrlen, 0);

    const SharedFD client =
            SharedFD::SocketClient(reinterpret_cast<const struct sockaddr*>(&addr), addrlen);
    ASSERT_TRUE(client);

    const SharedFD conn = SharedFD::Accept(*server);
    ASSERT_TRUE(conn);

    ASSERT_EQ(client->Write("test", 4), 4);

    char data[4];
    ASSERT_EQ(conn->Read(data, 4), 4);
    EXPECT_EQ(::memcmp(data, "test", 4), 0);
}

}  // namespace cuttlefish
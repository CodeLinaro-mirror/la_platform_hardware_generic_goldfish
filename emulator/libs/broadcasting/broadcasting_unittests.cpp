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

#include <vector>
#include <gtest/gtest.h>

#include "goldfish/broadcasting.h"

namespace {
using goldfish::broadcasting::Ticket;
using IntegerTopic = goldfish::broadcasting::Topic<int>;

struct MySubscriber {
    bool notify(const int x) {
        value = x;
        return wantMoreBroadcasts;
    }

    Ticket ticket;
    int value = 0;
    bool wantMoreBroadcasts = true;
};

using MySubscriberPtr = std::shared_ptr<MySubscriber>;
}  // namespace

TEST(broadcasting, example) {
    IntegerTopic integerTopic;
    std::vector<MySubscriberPtr> subscribers(5);
    for (MySubscriberPtr &s : subscribers) {
        s = std::make_shared<MySubscriber>();
    }

    integerTopic.broadcast(42);
    for (MySubscriberPtr &s : subscribers) {
        EXPECT_EQ(s->value, 0);  // not subscribed yet
        s->ticket = integerTopic.subscribe(s, &MySubscriber::notify);
        EXPECT_FALSE(s->ticket.empty());  // now subscribed
    }

    integerTopic.broadcast(42);
    for (const MySubscriberPtr &s : subscribers) {
        EXPECT_EQ(s->value, 42);
    }

    // unsubscribe two
    integerTopic.unsubscribe(&subscribers[0]->ticket);
    EXPECT_TRUE(subscribers[0]->ticket.empty());
    integerTopic.unsubscribe(&subscribers[2]->ticket);
    EXPECT_TRUE(subscribers[2]->ticket.empty());

    integerTopic.broadcast(77);
    EXPECT_EQ(subscribers[0]->value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[1]->value, 77);
    EXPECT_EQ(subscribers[2]->value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[3]->value, 77);
    EXPECT_EQ(subscribers[4]->value, 77);

    subscribers[3]->wantMoreBroadcasts = false;
    subscribers[4]->wantMoreBroadcasts = false;

    integerTopic.broadcast(15);
    EXPECT_EQ(subscribers[0]->value, 42);
    EXPECT_EQ(subscribers[1]->value, 15);
    EXPECT_EQ(subscribers[2]->value, 42);
    EXPECT_EQ(subscribers[3]->value, 15);  // this broadcast is still received
    EXPECT_EQ(subscribers[4]->value, 15);  // this broadcast is still received

    integerTopic.broadcast(99);
    EXPECT_EQ(subscribers[0]->value, 42);
    EXPECT_EQ(subscribers[1]->value, 99);
    EXPECT_EQ(subscribers[2]->value, 42);
    EXPECT_EQ(subscribers[3]->value, 15);  // unsubscribed, see `wantMoreBroadcasts` above
    EXPECT_EQ(subscribers[4]->value, 15);  // unsubscribed, see `wantMoreBroadcasts` above
}

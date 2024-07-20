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

using Topic = goldfish::broadcasting::Topic;
using IntegerTopic = goldfish::broadcasting::TopicT<int>;

namespace {
struct MySubscriber {
    bool notify(const int x) {
        value = x;
        return true;
    }

    static bool notifyS(void *that, const int x) {
        return static_cast<MySubscriber *>(that)->notify(x);
    }

    int value = 0;
    Topic::Ticket ticket = Topic::kEmptyTicket;
};
}  // namespace

TEST(broadcasting, subscribe) {
    std::vector<MySubscriber> subscribers(5);

    {
        IntegerTopic integerTopic;
        Topic::Ticket t = 0;
        for (MySubscriber &s : subscribers) {
            integerTopic.subscribe(&s, &MySubscriber::notifyS, &s.ticket);
            EXPECT_EQ(s.ticket, ++t);
        }
    }

    // `integerTopic` is no longer valid, the tickets must be empty
    for (const MySubscriber &s : subscribers) {
        EXPECT_EQ(s.ticket, Topic::kEmptyTicket);
    }
}

TEST(broadcasting, broadcast) {
    std::vector<MySubscriber> subscribers(5);
    IntegerTopic integerTopic;

    for (MySubscriber &s : subscribers) {
        integerTopic.subscribe(&s, &MySubscriber::notifyS, &s.ticket);
        EXPECT_NE(s.ticket, Topic::kEmptyTicket);
    }

    integerTopic.broadcast(42);
    for (const MySubscriber &s : subscribers) {
        EXPECT_EQ(s.value, 42);
    }

    integerTopic.broadcast(77);
    for (const MySubscriber &s : subscribers) {
        EXPECT_EQ(s.value, 77);
    }
}

TEST(broadcasting, unsubscribe) {
    std::vector<MySubscriber> subscribers(5);
    IntegerTopic integerTopic;

    for (MySubscriber &s : subscribers) {
        integerTopic.subscribe(&s, &MySubscriber::notifyS, &s.ticket);
        EXPECT_NE(s.ticket, Topic::kEmptyTicket);
    }

    EXPECT_EQ(subscribers[2].ticket, 3);
    EXPECT_EQ(subscribers[4].ticket, 5);

    integerTopic.unsubscribe(&subscribers[2].ticket);

    EXPECT_EQ(subscribers[4].ticket, 3);
}

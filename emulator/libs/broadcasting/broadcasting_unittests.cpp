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
using VoidTopic = goldfish::broadcasting::Topic<void>;

struct MySubscriber {
    std::optional<Ticket> notify(const int x) {
        value = x;

        if (wantMoreBroadcasts) {
            return std::nullopt;
        } else {
            return std::move(ticket);
        }
    }

    Ticket ticket;
    int value = 0;
    bool wantMoreBroadcasts = true;
};
}  // namespace

TEST(broadcasting, example) {
    IntegerTopic integerTopic;
    std::vector<MySubscriber> subscribers(5);

    integerTopic.broadcast(42);
    for (MySubscriber &s : subscribers) {
        EXPECT_EQ(s.value, 0);  // not subscribed yet
        s.ticket = std::move(integerTopic.subscribe(s, &MySubscriber::notify));
        EXPECT_TRUE(s.ticket.isSubscribed());  // now subscribed
    }

    integerTopic.broadcast(42);
    for (const MySubscriber &s : subscribers) {
        EXPECT_EQ(s.value, 42);
    }

    // unsubscribe two
    subscribers[0].ticket.unsubscribe();
    EXPECT_FALSE(subscribers[0].ticket.isSubscribed());
    subscribers[2].ticket.unsubscribe();
    EXPECT_FALSE(subscribers[2].ticket.isSubscribed());

    integerTopic.broadcast(77);
    EXPECT_EQ(subscribers[0].value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[1].value, 77);
    EXPECT_EQ(subscribers[2].value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[3].value, 77);
    EXPECT_EQ(subscribers[4].value, 77);

    subscribers[3].wantMoreBroadcasts = false;
    subscribers[4].wantMoreBroadcasts = false;

    integerTopic.broadcast(15);
    EXPECT_EQ(subscribers[0].value, 42);
    EXPECT_EQ(subscribers[1].value, 15);
    EXPECT_EQ(subscribers[2].value, 42);
    EXPECT_EQ(subscribers[3].value, 15);  // this broadcast is still received
    EXPECT_EQ(subscribers[4].value, 15);  // this broadcast is still received

    integerTopic.broadcast(99);
    EXPECT_EQ(subscribers[0].value, 42);
    EXPECT_EQ(subscribers[1].value, 99);
    EXPECT_EQ(subscribers[2].value, 42);
    EXPECT_EQ(subscribers[3].value, 15);  // unsubscribed, see `wantMoreBroadcasts` above
    EXPECT_EQ(subscribers[4].value, 15);  // unsubscribed, see `wantMoreBroadcasts` above

    subscribers[1].ticket.unsubscribe();  // all MUST explicitly unsubscribe

    for (const MySubscriber &s : subscribers) {
        EXPECT_FALSE(s.ticket.isSubscribed());
    }
}

TEST(broadcasting, build_test_TakesArgsReturnsVoid) {
    struct TakesArgsReturnsVoid {
        void notify(const int x) {}
    };

    IntegerTopic integerTopic;
    TakesArgsReturnsVoid subscriber;
    Ticket ticket = integerTopic.subscribe(subscriber,
                                           &TakesArgsReturnsVoid::notify);
    ticket.unsubscribe();
}

TEST(broadcasting, build_test_NoArgsReturnsMaybeTicket) {
    struct NoArgsReturnsMaybeTicket {
        std::optional<Ticket> notify() { return std::nullopt; }
    };

    VoidTopic voidTopic;
    NoArgsReturnsMaybeTicket subscriber;
    Ticket ticket = voidTopic.subscribe(subscriber,
                                        &NoArgsReturnsMaybeTicket::notify);
    ticket.unsubscribe();
}

TEST(broadcasting, build_test_NoArgsReturnsVoid) {
    struct NoArgsReturnsVoid {
        void notify() {}
    };

    VoidTopic voidTopic;
    NoArgsReturnsVoid subscriber;
    Ticket ticket = voidTopic.subscribe(subscriber,
                                        &NoArgsReturnsVoid::notify);
    ticket.unsubscribe();
}

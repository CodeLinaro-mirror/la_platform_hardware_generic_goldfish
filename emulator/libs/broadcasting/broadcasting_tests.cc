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

#include <gtest/gtest.h>

#include <vector>

#include "goldfish/broadcasting/broadcasting.h"

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
    auto integerTopic = IntegerTopic::Create();
    std::vector<MySubscriber> subscribers(5);

    integerTopic->Broadcast(42);
    for (MySubscriber& s : subscribers) {
        EXPECT_EQ(s.value, 0);  // not subscribed yet
        s.ticket = std::move(integerTopic->Subscribe(s, &MySubscriber::notify));
        EXPECT_TRUE(s.ticket.IsSubscribed());  // now subscribed
    }

    integerTopic->Broadcast(42);
    for (const MySubscriber& s : subscribers) {
        EXPECT_EQ(s.value, 42);
    }

    // unsubscribe two
    subscribers[0].ticket.Unsubscribe();
    EXPECT_FALSE(subscribers[0].ticket.IsSubscribed());
    subscribers[2].ticket.Unsubscribe();
    EXPECT_FALSE(subscribers[2].ticket.IsSubscribed());

    integerTopic->Broadcast(77);
    EXPECT_EQ(subscribers[0].value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[1].value, 77);
    EXPECT_EQ(subscribers[2].value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[3].value, 77);
    EXPECT_EQ(subscribers[4].value, 77);

    subscribers[3].wantMoreBroadcasts = false;
    subscribers[4].wantMoreBroadcasts = false;

    integerTopic->Broadcast(15);
    EXPECT_EQ(subscribers[0].value, 42);
    EXPECT_EQ(subscribers[1].value, 15);
    EXPECT_EQ(subscribers[2].value, 42);
    EXPECT_EQ(subscribers[3].value, 15);  // this broadcast is still received
    EXPECT_EQ(subscribers[4].value, 15);  // this broadcast is still received

    integerTopic->Broadcast(99);
    EXPECT_EQ(subscribers[0].value, 42);
    EXPECT_EQ(subscribers[1].value, 99);
    EXPECT_EQ(subscribers[2].value, 42);
    EXPECT_EQ(subscribers[3].value,
              15);  // unsubscribed, see `wantMoreBroadcasts` above
    EXPECT_EQ(subscribers[4].value,
              15);  // unsubscribed, see `wantMoreBroadcasts` above

    subscribers[1].ticket.Unsubscribe();  // all MUST explicitly unsubscribe

    for (const MySubscriber& s : subscribers) {
        EXPECT_FALSE(s.ticket.IsSubscribed());
    }
}

TEST(broadcasting, build_test_TakesArgsReturnsVoid) {
    struct TakesArgsReturnsVoid {
        void notify(const int x) {}
    };

    auto integerTopic = IntegerTopic::Create();
    TakesArgsReturnsVoid subscriber;
    Ticket ticket = integerTopic->Subscribe(subscriber, &TakesArgsReturnsVoid::notify);
    ticket.Unsubscribe();
}

TEST(broadcasting, build_test_NoArgsReturnsMaybeTicket) {
    struct NoArgsReturnsMaybeTicket {
        std::optional<Ticket> notify() { return std::nullopt; }
    };

    auto voidTopic = VoidTopic::Create();
    NoArgsReturnsMaybeTicket subscriber;
    Ticket ticket = voidTopic->Subscribe(subscriber, &NoArgsReturnsMaybeTicket::notify);
    ticket.Unsubscribe();
}

TEST(broadcasting, build_test_NoArgsReturnsVoid) {
    struct NoArgsReturnsVoid {
        void notify() {}
    };

    auto voidTopic = VoidTopic::Create();
    NoArgsReturnsVoid subscriber;
    Ticket ticket = voidTopic->Subscribe(subscriber, &NoArgsReturnsVoid::notify);
    ticket.Unsubscribe();
}

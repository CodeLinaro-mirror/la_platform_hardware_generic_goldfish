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

#include <functional>
#include <vector>

#include "goldfish/broadcasting/broadcasting.h"

namespace goldfish::broadcasting {
namespace {
struct MySubscriber {
    void notifyInt(const int x) {
        value = x;
        foreignCode();
    }

    void notifyNoArguments() {}

    std::function<void()> foreignCode = []() {};
    Subscription subscription;
    int value = 0;
};
}  // namespace

TEST(broadcasting, example) {
    auto integerTopic = Topic<int>::Create();
    std::vector<std::shared_ptr<MySubscriber>> subscribers(5);
    for (auto& s : subscribers) {
        s = std::make_shared<MySubscriber>();
        EXPECT_FALSE(s->subscription.IsSubscribed());
    }

    // the `Broadcast` call goes nowhere because nobody is subscribed yet
    EXPECT_EQ(integerTopic->Broadcast(42), 0);
    for (const std::shared_ptr<MySubscriber>& s : subscribers) {
        EXPECT_EQ(s->value, 0);
    }

    for (const std::shared_ptr<MySubscriber>& s : subscribers) {
        s->subscription = integerTopic->Subscribe(s, &MySubscriber::notifyInt);
        EXPECT_TRUE(s->subscription.IsSubscribed());  // now subscribed
    }

    EXPECT_EQ(integerTopic->Broadcast(42), 5);
    for (const std::shared_ptr<MySubscriber>& s : subscribers) {
        EXPECT_EQ(s->value, 42);
    }

    // unsubscribe two explicitly
    subscribers[0]->subscription.Unsubscribe();
    EXPECT_FALSE(subscribers[0]->subscription.IsSubscribed());
    subscribers[2]->subscription.Unsubscribe();
    EXPECT_FALSE(subscribers[2]->subscription.IsSubscribed());

    EXPECT_EQ(integerTopic->Broadcast(77), 3);

    EXPECT_EQ(subscribers[0]->value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[1]->value, 77);
    EXPECT_EQ(subscribers[2]->value, 42);  // unsubscribed above
    EXPECT_EQ(subscribers[3]->value, 77);
    EXPECT_EQ(subscribers[4]->value, 77);

    subscribers[1].reset();  // ~Subscription unsubscribes
    EXPECT_EQ(integerTopic->Broadcast(100), 2);
}

TEST(broadcasting, no_arguments_also_works) {
    auto voidTopic = Topic<>::Create();
    voidTopic->Subscribe(std::make_shared<MySubscriber>(), &MySubscriber::notifyNoArguments);
    voidTopic->Broadcast();
}

TEST(broadcasting, no_deadlock) {
    const auto topic = Topic<int>::Create();
    const auto subscriber = std::make_shared<MySubscriber>();
    const auto lateSubscriber = std::make_shared<MySubscriber>();
    Subscription lateSubscription;

    subscriber->foreignCode = [topic, lateSubscriber, &lateSubscription]() {
        lateSubscription = topic->Subscribe(lateSubscriber, &MySubscriber::notifyInt);
    };

    subscriber->subscription = topic->Subscribe(subscriber, &MySubscriber::notifyInt);

    EXPECT_EQ(topic->Broadcast(42), 1);
    EXPECT_EQ(subscriber->value, 42);
    EXPECT_EQ(lateSubscriber->value, 0);

    EXPECT_EQ(topic->Broadcast(77), 2);
    EXPECT_EQ(subscriber->value, 77);
    EXPECT_EQ(lateSubscriber->value, 77);
}

}  // namespace goldfish::broadcasting

/* Copyright (C) 2024 The Android Open Source Project
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

#pragma once
#include <cassert>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace goldfish {
namespace broadcasting {

/* See broadcasting_unittests.cpp for examples.
 *
 * `Topic<Args...>` a class to send broadcasts that take arguments
 * `Args...` to subscribers. Please note that `Args...` can be
 * `void` (see `Topic<void>` below) which means "no arguments".
 *
 * `Ticket` - represent a subscription handle. All subscriptions
 * MUST be explicily unsubscribed via `Ticket::unsubscribe()`,
 * see `~Ticket()`. Please note you CANNOT unsubscribe in your
 * class destructor (because a broadcast can arrive up to the
 * `Ticket::unsubscribe()` call).
 *
 * Callback:
 * 1. std::function<std::optional<Ticket>(Args...)>, it is taken
 *    by value and stored inside the `Topic` instance until you
 *    unsubscribe by calling`Ticket::unsubscribe()` explicitly
 *    or by returning `Ticket` from the callback. It is up to you
 *    how to manage the lifetimes of objects which your function
 *    depends on.
 * 2. The `Topic` class provides the `subscribe` call
 *    (see `Topic::subscribe`) to build the callback above from
 *    `T &` and a pointer to T's method. The same lifetime rules
 *    apply.
 *
 * Subscribing:
 * 1. Gives you a ticket to unsubscribe.
 * 2. Subscribing multiple times with the same arguments will create
 *    multiple subscriptions with different tickets.
 */

struct TopicBase;
template <class Callback>
struct TopicBaseTpl;
template <class... Args>
struct Topic;

struct Ticket {
    ~Ticket() { assert(!isSubscribed()); }

    Ticket() = default;

    Ticket(Ticket&& rhs) : Ticket(std::exchange(rhs.mTopic, nullptr), rhs.mValue) {}

    Ticket& operator=(Ticket&& rhs) {
        if (this != &rhs) {
            swap(*this, rhs);
        }
        return *this;
    }

    bool isSubscribed() const { return mTopic != nullptr; }
    void unsubscribe();

    static void swap(Ticket& lhs, Ticket& rhs) {
        using std::swap;
        swap(lhs.mTopic, rhs.mTopic);
        swap(lhs.mValue, rhs.mValue);
    }

    Ticket(const Ticket&) = delete;
    Ticket& operator=(const Ticket&) = delete;

  private:
    friend TopicBase;
    template <class Callback>
    friend struct TopicBaseTpl;
    template <class... Args>
    friend struct Topic;

    using value_t = unsigned;

    Ticket(TopicBase* const topic, const value_t value) : mTopic(topic), mValue(value) {}

    void release() { mTopic = nullptr; }

    TopicBase* mTopic = nullptr;
    value_t mValue = 0;
};

struct TopicBase {
    virtual ~TopicBase() {}

  private:
    friend Ticket;
    virtual void unsubscribeImpl(Ticket::value_t) = 0;
};

inline void Ticket::unsubscribe() {
    if (mTopic) {
        mTopic->unsubscribeImpl(mValue);
        mTopic = nullptr;
    }
}

template <class Callback>
struct TopicBaseTpl : public TopicBase {
    Ticket subscribe(Callback callback) {
        std::lock_guard<std::mutex> guard(mMutex);
        while (true) {
            const Ticket::value_t ticket = ++mLastTicket;
            const auto result = mSubscriptions.insert({ticket, {}});
            if (result.second) {
                result.first->second = std::move(callback);
                return Ticket(this, ticket);
            }
        }
    }

    TopicBaseTpl(const TopicBaseTpl&) = delete;
    TopicBaseTpl(TopicBaseTpl&&) = delete;
    TopicBaseTpl& operator=(const TopicBaseTpl&) = delete;
    TopicBaseTpl& operator=(TopicBaseTpl&&) = delete;

  protected:
    TopicBaseTpl() = default;

    std::unordered_map<Ticket::value_t, Callback> mSubscriptions;
    Ticket::value_t mLastTicket = {};
    std::mutex mMutex;

  private:
    void unsubscribeImpl(const Ticket::value_t ticket) override {
        std::lock_guard<std::mutex> guard(mMutex);
        mSubscriptions.erase(ticket);
    }
};

template <class... Args>
struct Topic : public TopicBaseTpl<std::function<std::optional<Ticket>(Args...)>> {
    using Callback = std::function<std::optional<Ticket>(Args...)>;
    using TopicT = TopicBaseTpl<Callback>;
    using TopicT::mMutex;
    using TopicT::mSubscriptions;
    using TopicT::subscribe;

    template <class T>
    Ticket subscribe(T& object, std::optional<Ticket> (T::*const method)(Args...)) {
        return subscribe([&object, method](Args... args) {
            return (object.*method)(std::forward<Args>(args)...);
        });
    }

    template <class T>
    Ticket subscribe(T& object, void (T::*const method)(Args...)) {
        return subscribe([&object, method](Args... args) {
            (object.*method)(std::forward<Args>(args)...);
            return std::nullopt;
        });
    }

    void broadcast(Args... args) {
        std::lock_guard<std::mutex> guard(mMutex);
        auto i = mSubscriptions.begin();
        while (i != mSubscriptions.end()) {
            std::optional<Ticket> result = (i->second)(std::forward<Args>(args)...);
            if (result.has_value()) {
                assert(result->mTopic == this);
                assert(result->mValue == i->first);
                result->release();
                i = mSubscriptions.erase(i);
            } else {
                ++i;
            }
        }
    }
};

template <>
struct Topic<void> : public TopicBaseTpl<std::function<std::optional<Ticket>()>> {
    using Callback = std::function<std::optional<Ticket>(void)>;
    using TopicT = TopicBaseTpl<Callback>;
    using TopicT::mMutex;
    using TopicT::mSubscriptions;
    using TopicT::subscribe;

    template <class T>
    Ticket subscribe(T& object, std::optional<Ticket> (T::*const method)()) {
        return subscribe([&object, method]() { return (object.*method)(); });
    }

    template <class T>
    Ticket subscribe(T& object, void (T::*const method)()) {
        return subscribe([&object, method]() {
            (object.*method)();
            return std::nullopt;
        });
    }

    void broadcast() {
        std::lock_guard<std::mutex> guard(mMutex);
        auto i = mSubscriptions.begin();
        while (i != mSubscriptions.end()) {
            std::optional<Ticket> result = (i->second)();
            if (result.has_value()) {
                assert(result->mTopic == this);
                assert(result->mValue == i->first);
                result->release();
                i = mSubscriptions.erase(i);
            } else {
                ++i;
            }
        }
    }
};

}  // namespace broadcasting
}  // namespace goldfish

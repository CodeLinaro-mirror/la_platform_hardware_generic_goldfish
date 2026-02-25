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
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace goldfish::broadcasting {

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
    ~Ticket() { assert(!IsSubscribed()); }

    Ticket() = default;

    Ticket(Ticket&& rhs) : Ticket(std::move(rhs.topic_), rhs.value_) {}

    Ticket& operator=(Ticket&& rhs) {
        if (this != &rhs) {
            Swap(*this, rhs);
        }
        return *this;
    }

    bool IsSubscribed() const { return topic_.use_count() > 0; }
    void Unsubscribe();

    static void Swap(Ticket& lhs, Ticket& rhs) {
        using std::swap;
        swap(lhs.topic_, rhs.topic_);
        swap(lhs.value_, rhs.value_);
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

    Ticket(std::weak_ptr<TopicBase> topic, const value_t value)
            : topic_(std::move(topic)), value_(value) {}

    void Release() { topic_.reset(); }

    std::weak_ptr<TopicBase> topic_;
    value_t value_ = 0;
};

struct TopicBase : std::enable_shared_from_this<TopicBase> {
    virtual ~TopicBase() = default;

  private:
    friend Ticket;
    virtual void UnsubscribeImpl(Ticket::value_t) = 0;
};

inline void Ticket::Unsubscribe() {
    const auto pinned = topic_.lock();
    if (pinned) {
        pinned->UnsubscribeImpl(value_);
        Release();
    }
}

template <class Callback>
struct TopicBaseTpl : public TopicBase {
    Ticket Subscribe(Callback callback) {
        const std::lock_guard<std::mutex> guard(mutex_);
        while (true) {
            const Ticket::value_t ticket = ++last_ticket_;
            const auto result = subscriptions_.insert({ticket, {}});
            if (result.second) {
                result.first->second = std::move(callback);
                return Ticket(shared_from_this(), ticket);
            }
        }
    }

    TopicBaseTpl(const TopicBaseTpl&) = delete;
    TopicBaseTpl(TopicBaseTpl&&) = delete;
    TopicBaseTpl& operator=(const TopicBaseTpl&) = delete;
    TopicBaseTpl& operator=(TopicBaseTpl&&) = delete;

  protected:
    TopicBaseTpl() = default;

    std::unordered_map<Ticket::value_t, Callback> subscriptions_;
    Ticket::value_t last_ticket_ = {};
    std::mutex mutex_;

  private:
    void UnsubscribeImpl(const Ticket::value_t ticket) override {
        const std::lock_guard<std::mutex> guard(mutex_);
        subscriptions_.erase(ticket);
    }
};

template <class... Args>
struct Topic : public TopicBaseTpl<std::function<std::optional<Ticket>(Args...)>> {
  private:
    struct Private {};

  public:
    using Callback = std::function<std::optional<Ticket>(Args...)>;
    using TopicT = TopicBaseTpl<Callback>;
    using TopicT::mutex_;
    using TopicT::Subscribe;
    using TopicT::subscriptions_;

    explicit Topic(Private) {}

    static std::shared_ptr<Topic> Create() { return std::make_shared<Topic>(Private()); }

    template <class T>
    Ticket Subscribe(T& object, std::optional<Ticket> (T::* const method)(Args...)) {
        return Subscribe([&object, method](Args... args) {
            return (object.*method)(std::forward<Args>(args)...);
        });
    }

    template <class T>
    Ticket Subscribe(T& object, void (T::* const method)(Args...)) {
        return Subscribe([&object, method](Args... args) {
            (object.*method)(std::forward<Args>(args)...);
            return std::nullopt;
        });
    }

    void Broadcast(Args... args) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto i = subscriptions_.begin();
        while (i != subscriptions_.end()) {
            std::optional<Ticket> result = (i->second)(std::forward<Args>(args)...);
            if (result.has_value()) {
                assert(result->topic_.lock().get() == this);
                assert(result->value_ == i->first);
                result->Release();
                i = subscriptions_.erase(i);
            } else {
                ++i;
            }
        }
    }
};

}  // namespace goldfish::broadcasting

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
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace goldfish {
namespace broadcasting {

/* See broadcasting_unittests.cpp for examples.
 *
 * `Topic<Args...>` a class to send broadcasts that take arguments
 * `Args...` to subscribers. Please note that `Args...` can be
 * `void` (see `Topic<void>` below) which means "no arguments".

 * Callback:
 * 1. std::function<bool(Args...)>, it is taken by value and stored
 *    inside the `Topic` instance until you unsubscribe by calling
 *    `Topic::unsubscribe` explicitly or by returning `false` from
 *    the callback. It is up to you how to manage the lifetimes of
 *    objects which you function depends on.
 * 2. The `Topic` class provides the `subscribe` call
 *    (see `Topic::subscribe`) to build the function above from
 *    `const std::shared_ptr<T> &` and a pointer to T's method.
 *    The callback stores a weak pointer to `T` and calling
 *    `Topic::unsubscribe` is optional in this case: the callback
 *     will return `false` once `T` is destroyed.
 *
 * Subscribing:
 * 1. Gives you a ticket to unsubscribe.
 * 2. Subscribing multiple times with the same arguments will create
 *    multiple subscriptions with different tickets.
 *
 * Explicit unsubsribing (`Topic::unsubscribe`):
 * 1. Passing an empty (see `bool Ticket::empty() const`) ticket has
 *    no effect.
 * 2. optional, if your callback handles lifetimes of its dependencies.
 * 3. calling `Topic::unsubscribe` with the same `Ticket` multiple
 *    times is undefined behavior:`Ticket` values could be reused,
 *    which means you would be unsubscribing someone else), see
 *   `Topic::unsubscribe(Ticket *ticket)` to clear the ticket value.
 */

struct Ticket {
    using value_t = unsigned;
    static constexpr value_t kEmpty = 0;
    value_t value = kEmpty;
    bool empty() const { return value == kEmpty; }
};

template <class Callback> struct TopicBaseTpl {
    Ticket subscribe(Callback callback) {
        std::lock_guard<std::mutex> guard(mMutex);
        while (true) {
            const Ticket::value_t ticket = generateTicket();
            const auto result = mSubscriptions.insert({ticket, {}});
            if (result.second) {
                result.first->second = std::move(callback);
                return { .value = ticket };
            }
        }
    }

    void unsubscribe(const Ticket ticket) {
        const auto value = ticket.value;
        if (value) {
            std::lock_guard<std::mutex> guard(mMutex);
            mSubscriptions.erase(value);
        }
    }

    void unsubscribe(Ticket *const ticket) {
        unsubscribe(*ticket);
        ticket->value = Ticket::kEmpty;
    }

    TopicBaseTpl(const TopicBaseTpl &) = delete;
    TopicBaseTpl(TopicBaseTpl &&) = delete;
    TopicBaseTpl& operator=(const TopicBaseTpl &) = delete;
    TopicBaseTpl& operator=(TopicBaseTpl &&) = delete;

protected:
    TopicBaseTpl() = default;

    Ticket::value_t generateTicket() {
        const Ticket::value_t ticket = ++mLastTicket;
        if (ticket != Ticket::kEmpty) {
            return ticket;
        } else {
            return ++mLastTicket;
        }
    }

    std::unordered_map<Ticket::value_t, Callback> mSubscriptions;
    Ticket::value_t mLastTicket = Ticket::kEmpty;
    std::mutex mMutex;
};

template <class... Args> struct Topic : public TopicBaseTpl<std::function<bool(Args...)>> {
    using Callback = std::function<bool(Args...)>;
    using TopicBase = TopicBaseTpl<Callback>;
    using TopicBase::subscribe;
    using TopicBase::mSubscriptions;
    using TopicBase::mMutex;

    template <class T> Ticket subscribe(const std::shared_ptr<T> &object,
                                        bool (T::*const method)(Args...)) {
        auto weakObject = std::weak_ptr<T>(object);

        return subscribe([method, weakObject = std::move(weakObject)](Args... args){
            const auto object = weakObject.lock();
            return object && (*object.*method)(std::forward<Args>(args)...);
        });
    }

    void broadcast(Args... args) {
        std::lock_guard<std::mutex> guard(mMutex);
        auto i = mSubscriptions.begin();
        while (i != mSubscriptions.end()) {
            if ((i->second)(std::forward<Args>(args)...)) {
                ++i;
            } else {
                i = mSubscriptions.erase(i);
            }
        }
    }
};

template <> struct Topic<void> : public TopicBaseTpl<std::function<bool()>> {
    using Callback = std::function<bool()>;
    using TopicBase = TopicBaseTpl<Callback>;
    using TopicBase::subscribe;
    using TopicBase::mSubscriptions;
    using TopicBase::mMutex;

    template <class T> Ticket subscribe(const std::shared_ptr<T> &object,
                                        bool (T::*const method)()) {
        auto weakObject = std::weak_ptr<T>(object);

        return subscribe([method, weakObject = std::move(weakObject)](){
            const auto object = weakObject.lock();
            return object && (*object.*method)();
        });
    }

    void broadcast() {
        std::lock_guard<std::mutex> guard(mMutex);
        auto i = mSubscriptions.begin();
        while (i != mSubscriptions.end()) {
            if ((i->second)()) {
                ++i;
            } else {
                i = mSubscriptions.erase(i);
            }
        }
    }
};

}  // namespace broadcasting
}  // namespace goldfish

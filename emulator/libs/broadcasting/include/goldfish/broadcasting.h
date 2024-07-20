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
#include <mutex>
#include <vector>
#include <utility>

namespace goldfish {
namespace broadcasting {

struct Topic {
    using Ticket = unsigned;
    using UpdateTicketFn = void (*)(void *, Ticket newTicket);
    static constexpr Ticket kEmptyTicket = 0;

    ~Topic();

    void unsubscribe(Ticket *ticket);

    Topic(const Topic &) = delete;
    Topic(Topic &&) = delete;
    Topic& operator=(const Topic &) = delete;
    Topic& operator=(Topic &&) = delete;

protected:
    Topic() = default;

    struct Subscription {
        Ticket *pTicket;
        void *object;
        void *notifyFn;
    };

    void subscribeImpl(void *object, void *notifyFn, Ticket *pTicket);
    void unsubscribeImpl(size_t i, size_t size);

    std::vector<Subscription> mSubscriptions;
    mutable std::mutex mMutex;
};

template <class... Args> struct TopicT : public Topic {
    using NotifyFn = bool (*)(void *, Args...);

    void subscribe(void *object, NotifyFn notifyFn, Ticket *pTicket) {
        return subscribeImpl(object, reinterpret_cast<void *>(notifyFn), pTicket);
    }

    void broadcast(Args... args) {
        std::lock_guard<std::mutex> guard(mMutex);
        size_t i = 0;
        size_t size = mSubscriptions.size();

        while (i < size) {
            Subscription &si = mSubscriptions[i];
            if ((*reinterpret_cast<NotifyFn>(si.notifyFn))(
                        si.object, std::forward<Args>(args)...)) {
                ++i;
            } else {
                unsubscribeImpl(i, size);
                --size;
            }
        }

        mSubscriptions.resize(size);
    }
};

template <> struct TopicT<void> : public Topic {
    using NotifyFn = bool (*)(void *);

    void subscribe(void *object, NotifyFn notifyFn, Ticket *pTicket) {
        return subscribeImpl(object, reinterpret_cast<void *>(notifyFn), pTicket);
    }

    void broadcast() {
        std::lock_guard<std::mutex> guard(mMutex);
        size_t i = 0;
        size_t size = mSubscriptions.size();

        while (i < size) {
            Subscription &si = mSubscriptions[i];
            if ((*reinterpret_cast<NotifyFn>(si.notifyFn))(si.object)) {
                ++i;
            } else {
                unsubscribeImpl(i, size);
                --size;
            }
        }

        mSubscriptions.resize(size);
    }
};

}  // namespace broadcasting
}  // namespace goldfish

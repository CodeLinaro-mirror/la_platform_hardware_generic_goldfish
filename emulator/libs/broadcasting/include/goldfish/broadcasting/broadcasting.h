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
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"

// See broadcasting_unittests.cpp for examples.
// NOTE: to avoid deadlocks `Topic::Broadcast` copies all alive subscriptions.

namespace goldfish::broadcasting {

struct TopicBase;
template <class... Args>
class Topic;

struct Subscription {
    ~Subscription() { Unsubscribe(); }

    Subscription() = default;
    Subscription(Subscription&& rhs)
            : topic_(std::move(rhs.topic_)), id_(std::exchange(rhs.id_, 0)) {}

    Subscription& operator=(Subscription&& rhs) {
        if (this != &rhs) {
            swap(*this, rhs);
        }
        return *this;
    }

    bool IsSubscribed() const { return topic_.use_count() > 0; }
    void Unsubscribe();

    friend void swap(Subscription& lhs,  // NOLINT(readability-identifier-naming)
                     Subscription& rhs) {
        using std::swap;
        swap(lhs.topic_, rhs.topic_);
        swap(lhs.id_, rhs.id_);
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

  private:
    friend TopicBase;
    template <class... Args>
    friend class Topic;
    using ID = uint64_t;

    Subscription(std::weak_ptr<TopicBase> topic, const ID id) : topic_(std::move(topic)), id_(id) {
        DCHECK(id_ > 0);
    }

    std::weak_ptr<TopicBase> topic_;
    ID id_ = 0;
};

struct TopicBase : std::enable_shared_from_this<TopicBase> {
    virtual ~TopicBase() = default;

  private:
    friend Subscription;
    virtual void Unsubscribe(Subscription::ID) = 0;
};

inline void Subscription::Unsubscribe() {
    if (const auto pinned = topic_.lock()) {
        DCHECK(id_ > 0);
        pinned->Unsubscribe(id_);
        topic_.reset();
        id_ = 0;
    }
}

template <class... Args>
class Topic : public TopicBase {
    struct Private {};

  public:
    explicit Topic(Private) {}

    static std::shared_ptr<Topic> Create() { return std::make_shared<Topic>(Private()); }

    template <typename S>
    Subscription Subscribe(const std::shared_ptr<S>& subscriber, void (S::*method)(Args...)) {
        return SubscribeImpl(subscriber, [method](void* subscriber, auto&&... args) {
            return (static_cast<S*>(subscriber)->*method)(std::forward<decltype(args)>(args)...);
        });
    }

    size_t Broadcast(Args... args) {
        const auto live_subscriptions = CopyLiveSubscriptions();
        for (const auto& s : live_subscriptions) {
            s.trampoline(s.subscriber.get(), std::forward<decltype(args)>(args)...);
        }
        return live_subscriptions.size();
    }

    Topic(const Topic&) = delete;
    Topic(Topic&&) = delete;
    Topic& operator=(const Topic&) = delete;
    Topic& operator=(Topic&&) = delete;

  private:
    using Trampoline = std::function<void(void*, Args...)>;

    struct LiveSubscriptionEntry {
        std::shared_ptr<void> subscriber;
        Trampoline trampoline;
    };

    std::vector<LiveSubscriptionEntry> CopyLiveSubscriptions() {
        std::vector<LiveSubscriptionEntry> live_subscriptions;

        const absl::MutexLock lock(mutex_);
        live_subscriptions.reserve(subscriptions_.size());

        auto i = subscriptions_.begin();
        while (i != subscriptions_.end()) {
            if (auto live_subscriber = i->second.subscriber.lock()) {
                live_subscriptions.push_back({
                    .subscriber = std::move(live_subscriber),
                    .trampoline = i->second.trampoline,
                });

                ++i;
            } else {
                subscriptions_.erase(i++);
            }
        }

        return live_subscriptions;
    }

    struct SubscriptionEntry {
        std::weak_ptr<void> subscriber;
        Trampoline trampoline;
    };

    Subscription SubscribeImpl(const std::shared_ptr<void>& subscriber, Trampoline trampoline) {
        const absl::MutexLock lock(mutex_);
        const auto subscription_id = ++last_subscription_id_;
        const bool inserted = subscriptions_
                                      .insert({subscription_id,
                                               {
                                                   .subscriber = subscriber,
                                                   .trampoline = std::move(trampoline),
                                               }})
                                      .second;
        DCHECK(inserted);
        return Subscription(shared_from_this(), subscription_id);
    }

    void Unsubscribe(const Subscription::ID subscription_id) override {
        const absl::MutexLock lock(mutex_);
        const size_t erased_num = subscriptions_.erase(subscription_id);
        DCHECK(erased_num == 1);
    }

    absl::flat_hash_map<Subscription::ID, SubscriptionEntry> subscriptions_ ABSL_GUARDED_BY(mutex_);
    Subscription::ID last_subscription_id_ ABSL_GUARDED_BY(mutex_) = 0;
    absl::Mutex mutex_;
};

}  // namespace goldfish::broadcasting

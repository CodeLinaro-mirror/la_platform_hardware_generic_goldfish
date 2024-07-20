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

#include "goldfish/broadcasting.h"

namespace goldfish {
namespace broadcasting {

Topic::~Topic() {
    for (const Subscription &s : mSubscriptions) {
        *s.pTicket = kEmptyTicket;
    }
}

void Topic::subscribeImpl(void *object,
                          void *notifyFn,
                          Ticket *pTicket) {
    std::lock_guard<std::mutex> guard(mMutex);
    mSubscriptions.push_back({
        .pTicket = pTicket,
        .object = object,
        .notifyFn = notifyFn,
    });

    *pTicket = mSubscriptions.size();
}

void Topic::unsubscribe(Ticket *pTicket) {
    std::lock_guard<std::mutex> guard(mMutex);
    const Ticket ticket = *pTicket;
    if (ticket == 0) {
        return;
    }

    unsubscribeImpl(ticket - 1, mSubscriptions.size());
    mSubscriptions.pop_back();
}

void Topic::unsubscribeImpl(const size_t i, const size_t size) {
    Subscription &si = mSubscriptions[i];
    *si.pTicket = kEmptyTicket;

    const size_t i1 = i + 1;
    if (i1 < size) {
        si = mSubscriptions[size - 1];
        *si.pTicket = i1;
    }
}

}  // namespace broadcasting
}  // namespace goldfish

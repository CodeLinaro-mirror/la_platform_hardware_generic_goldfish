/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/base/UniqueHandle.h"

#include <gtest/gtest.h>

using goldfish::base::UniqueHandle;

struct StatelessIntDeleter {
    struct Empty {};
    StatelessIntDeleter(Empty) {}
    StatelessIntDeleter() = default;
    void operator()(int x) const {}
};

struct StatefulIntDeleter {
    struct Empty {};
    StatefulIntDeleter(Empty) {}
    StatefulIntDeleter(int* sumIntoRef) : mSumIntoRef(sumIntoRef) {}

    StatefulIntDeleter(StatefulIntDeleter&& other) noexcept
            : mSumIntoRef(std::exchange(other.mSumIntoRef, nullptr)) {}

    StatefulIntDeleter& operator=(StatefulIntDeleter&& other) noexcept {
        mSumIntoRef = std::exchange(other.mSumIntoRef, nullptr);
        return *this;
    }

    void operator()(int x) const {
        if (mSumIntoRef) {
            *mSumIntoRef += x;
        }
    }

    int* mSumIntoRef = nullptr;
};

struct NothrowMoveDeleter {
    struct Empty {};
    void operator()(int) const {}
    NothrowMoveDeleter() = default;
    NothrowMoveDeleter(Empty) {}
    NothrowMoveDeleter(NothrowMoveDeleter&&) noexcept = default;
    NothrowMoveDeleter& operator=(NothrowMoveDeleter&&) noexcept = default;
};

struct ThrowingMoveDeleter {
    struct Empty {};
    void operator()(int) const {}
    ThrowingMoveDeleter() = default;
    ThrowingMoveDeleter(Empty) {}
    ThrowingMoveDeleter(ThrowingMoveDeleter&&) noexcept(false) {}
    ThrowingMoveDeleter& operator=(ThrowingMoveDeleter&&) noexcept(false) { return *this; }
};

using UniqueHandleOfInt = UniqueHandle<int, -1, StatelessIntDeleter>;
using UniqueHandleOfIntStatefulDeleter = UniqueHandle<int, -1, StatefulIntDeleter>;

// Empty base optimization (https://en.cppreference.com/w/cpp/language/ebo.html)
static_assert(sizeof(UniqueHandleOfInt) == sizeof(int));

// noexcept correctness checks
static_assert(std::is_nothrow_move_constructible_v<UniqueHandle<int, -1, NothrowMoveDeleter>>);
static_assert(!std::is_nothrow_move_constructible_v<UniqueHandle<int, -1, ThrowingMoveDeleter>>);
static_assert(noexcept(swap(std::declval<UniqueHandle<int, -1, NothrowMoveDeleter>&>(),
                            std::declval<UniqueHandle<int, -1, NothrowMoveDeleter>&>())));

TEST(UniqueHandle, default_empty) {
    UniqueHandleOfInt q;
    EXPECT_FALSE(q);
    q.reset(42);
    EXPECT_TRUE(q);
}

TEST(UniqueHandle, get_release) {
    UniqueHandleOfInt q(42);
    EXPECT_EQ(q.get(), 42);

    EXPECT_EQ(q.get(), 42);
    EXPECT_EQ(q.get(), 42);
    EXPECT_EQ(q.get(), 42);

    EXPECT_EQ(q.release(), 42);
    EXPECT_EQ(q.release(), -1);
    EXPECT_EQ(q.release(), -1);

    EXPECT_FALSE(q.ok());
}

TEST(UniqueHandle, move_ctor) {
    UniqueHandleOfInt from(42);
    EXPECT_EQ(from.get(), 42);

    UniqueHandleOfInt to(std::move(from));

    EXPECT_EQ(to.get(), 42);
    EXPECT_FALSE(from.ok());
}

TEST(UniqueHandle, move_assign) {
    UniqueHandleOfInt from(42);
    EXPECT_TRUE(from.ok());

    UniqueHandleOfInt to;
    EXPECT_FALSE(to.ok());

    to = std::move(from);
    EXPECT_EQ(to.get(), 42);
    EXPECT_FALSE(from.ok());
}

TEST(UniqueHandle, move_assign2) {
    UniqueHandleOfInt from(42);
    EXPECT_EQ(from.get(), 42);

    UniqueHandleOfInt to(37);
    EXPECT_EQ(to.get(), 37);

    to = std::move(from);
    EXPECT_EQ(to.get(), 42);
    EXPECT_FALSE(from.ok());
}

TEST(UniqueHandle, move_assign_self) {
    UniqueHandleOfInt a(42);
    EXPECT_EQ(a.get(), 42);

    a = std::move(a);
    EXPECT_EQ(a.get(), 42);
}

TEST(UniqueHandle, move_assign_stateful) {
    int sumIntoFrom = 0;
    int sumIntoTo = 0;
    {
        UniqueHandleOfIntStatefulDeleter from(42, StatefulIntDeleter(&sumIntoFrom));
        UniqueHandleOfIntStatefulDeleter to(10, StatefulIntDeleter(&sumIntoTo));

        // After this, `from` will hold the original contents of `to`.
        to = std::move(from);
        EXPECT_FALSE(from.ok());
        EXPECT_EQ(to.get(), 42);
    }
    // `from` (which now holds handle 10 and deleter with `sumIntoTo`) is destructed.
    EXPECT_EQ(sumIntoTo, 10);
    // `to` (which now holds handle 42 and deleter with `sumIntoFrom`) is destructed.
    EXPECT_EQ(sumIntoFrom, 42);
}

TEST(UniqueHandle, swap) {
    UniqueHandleOfInt a(42);
    UniqueHandleOfInt b(5);

    EXPECT_EQ(a.get(), 42);
    EXPECT_EQ(b.get(), 5);

    swap(a, b);

    EXPECT_EQ(a.get(), 5);
    EXPECT_EQ(b.get(), 42);
}

TEST(UniqueHandle, dctor) {
    int sumInto = 0;
    {
        UniqueHandleOfIntStatefulDeleter q1(1, StatefulIntDeleter(&sumInto));
        {
            UniqueHandleOfIntStatefulDeleter q2(2, StatefulIntDeleter(&sumInto));
            {
                UniqueHandleOfIntStatefulDeleter q3(3, StatefulIntDeleter(&sumInto));
                EXPECT_EQ(sumInto, 0);
            }
            EXPECT_EQ(sumInto, 3);
        }
        EXPECT_EQ(sumInto, 3 + 2);
    }
    EXPECT_EQ(sumInto, 3 + 2 + 1);
}

TEST(UniqueHandle, swap_stateful) {
    int sumA = 0;
    int sumB = 0;

    UniqueHandleOfIntStatefulDeleter a(10, StatefulIntDeleter(&sumA));
    UniqueHandleOfIntStatefulDeleter b(20, StatefulIntDeleter(&sumB));

    swap(a, b);

    EXPECT_EQ(a.get(), 20);
    EXPECT_EQ(b.get(), 10);

    a.reset();
    EXPECT_EQ(sumA, 0);
    EXPECT_EQ(sumB, 20);

    b.reset();
    EXPECT_EQ(sumA, 10);
    EXPECT_EQ(sumB, 20);
}

TEST(UniqueHandle, reset) {
    int sumInto = 0;

    UniqueHandleOfIntStatefulDeleter q(10, StatefulIntDeleter(&sumInto));
    EXPECT_TRUE(q.ok());

    q.reset();
    EXPECT_FALSE(q.ok());
    EXPECT_EQ(sumInto, 10);

    q.reset(20);
    EXPECT_TRUE(q.ok());
    EXPECT_EQ(q.get(), 20);
    EXPECT_EQ(sumInto, 10);

    q.reset(30);
    EXPECT_TRUE(q.ok());
    EXPECT_EQ(q.get(), 30);
    EXPECT_EQ(sumInto, 10 + 20);
}

TEST(UniqueHandle, default_constructor_stateful_deleter) {
    UniqueHandleOfIntStatefulDeleter q;
    EXPECT_FALSE(q.ok());
}

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
    void operator()(int x) const {}
};

struct StatefullIntDeleter {
    StatefullIntDeleter(int* sumIntoRef) : mSumIntoRef(sumIntoRef) {}

    void operator()(int x) const { *mSumIntoRef += x; }

    int* mSumIntoRef;
};

using UniqueHandleOfInt = UniqueHandle<int, -1, StatelessIntDeleter>;
using UniqueHandleOfIntStatefullDeleter = UniqueHandle<int, -1, StatefullIntDeleter>;

// Empty base optimization (https://en.cppreference.com/w/cpp/language/ebo.html)
static_assert(sizeof(UniqueHandleOfInt) == sizeof(int));

TEST(UniqueHandle, default_empty) {
    UniqueHandleOfInt q;
    EXPECT_FALSE(q.ok());
}

TEST(UniqueHandle, get_release) {
    UniqueHandleOfInt q(42);
    EXPECT_TRUE(q.ok());

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
    EXPECT_TRUE(from.ok());

    UniqueHandleOfInt to(std::move(from));

    EXPECT_TRUE(to.ok());
    EXPECT_FALSE(from.ok());
}

TEST(UniqueHandle, move_assign) {
    UniqueHandleOfInt from(42);
    EXPECT_TRUE(from.ok());

    UniqueHandleOfInt to;
    EXPECT_FALSE(to.ok());

    to = std::move(from);
    EXPECT_TRUE(to.ok());
    EXPECT_FALSE(from.ok());
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
        UniqueHandleOfIntStatefullDeleter q1(1, StatefullIntDeleter(&sumInto));
        {
            UniqueHandleOfIntStatefullDeleter q2(2, StatefullIntDeleter(&sumInto));
            {
                UniqueHandleOfIntStatefullDeleter q3(3, StatefullIntDeleter(&sumInto));
                EXPECT_EQ(sumInto, 0);
            }
            EXPECT_EQ(sumInto, 3);
        }
        EXPECT_EQ(sumInto, 3 + 2);
    }
    EXPECT_EQ(sumInto, 3 + 2 + 1);
}

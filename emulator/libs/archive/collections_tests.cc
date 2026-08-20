// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>
#include <string>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/collections/deque.h"
#include "goldfish/archive/collections/pair.h"
#include "goldfish/archive/collections/set.h"
#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/collections/vector.h"
#include "goldfish/archive/deque_archive.h"

namespace goldfish::archive {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;

using goldfish::archive::DequeArchive;

namespace {

struct Person {
    std::string name;
    int age = 0;

    bool operator==(const Person&) const = default;
};

IWriter& operator<<(IWriter& w, const Person& p) {
    return w << p.name << p.age;
}

absl::Status ReadValue(IReader& r, Person& p) {
    return ReadValue(r, p.name, p.age);
}

}  // namespace

TEST(deque, positive) {
    DequeArchive archive;

    const std::deque<Person> empty;
    const std::deque<Person> people = {{"Alice", 30}, {"Bob", 25}};

    archive << empty << people;

    {
        std::deque<Person> read_empty;
        ASSERT_THAT(ReadValue(archive, read_empty), IsOk());
        EXPECT_THAT(read_empty, IsEmpty());
    }

    {
        std::deque<Person> read_people;
        ASSERT_THAT(ReadValue(archive, read_people), IsOk());
        EXPECT_THAT(read_people, ElementsAre(Person{"Alice", 30}, Person{"Bob", 25}));
    }
}

// Tests read failure when an individual non-POD element fails to deserialize
TEST(deque, negative) {
    // 1. Valid size, but payload cuts off mid-stream
    {
        DequeArchive archive;
        archive << static_cast<size_t>(2);
        archive << Person{"Alice", 30};  // Only 1 written, 2 expected

        std::deque<Person> people;
        EXPECT_THAT(ReadValue(archive, people), Not(IsOk()));
    }

    // 2. Completely empty archive
    {
        DequeArchive empty_archive;
        std::deque<Person> people;
        EXPECT_THAT(ReadValue(empty_archive, people), Not(IsOk()));
    }
}

TEST(pair, positive) {
    DequeArchive archive;

    const std::pair<int, std::string> int_str_pair = {-42, "hello"};
    const std::pair<std::string, Person> complex_pair = {"admin", Person{"Alice", 30}};

    archive << int_str_pair << complex_pair;

    {
        std::pair<int, std::string> read_pair;
        ASSERT_THAT(ReadValue(archive, read_pair), IsOk());
        EXPECT_EQ(read_pair, (std::pair<int, std::string>{-42, "hello"}));
    }
    {
        std::pair<std::string, Person> read_pair;
        ASSERT_THAT(ReadValue(archive, read_pair), IsOk());
        EXPECT_EQ(read_pair.first, "admin");
        EXPECT_EQ(read_pair.second, (Person{"Alice", 30}));
    }
}

TEST(pair, negative) {
    // 1. First element written, second element missing
    {
        DequeArchive archive;
        archive << -42;  // Only first element of std::pair<int, std::string>

        std::pair<int, std::string> read_pair;
        EXPECT_THAT(ReadValue(archive, read_pair), Not(IsOk()));
    }

    // 2. Second element is corrupted / cut off mid-deserialization
    {
        DequeArchive archive;
        archive << "admin";
        archive << static_cast<size_t>(2);  // Missing the actual fields for Person

        std::pair<std::string, Person> read_pair;
        EXPECT_THAT(ReadValue(archive, read_pair), Not(IsOk()));
    }

    // 3. Completely empty archive
    {
        DequeArchive empty_archive;
        std::pair<int, std::string> read_pair;
        EXPECT_THAT(ReadValue(empty_archive, read_pair), Not(IsOk()));
    }
}

TEST(set, positive) {
    DequeArchive archive;

    const std::set<int> empty_set;
    const std::set<int> int_set = {-42, 0, 7, 100};

    archive << empty_set << int_set;

    {
        std::set<int> read_empty;
        ASSERT_THAT(ReadValue(archive, read_empty), IsOk());
        EXPECT_THAT(read_empty, IsEmpty());
    }
    {
        std::set<int> read_set;
        ASSERT_THAT(ReadValue(archive, read_set), IsOk());
        EXPECT_THAT(read_set, ElementsAre(-42, 0, 7, 100));
    }
}

TEST(set, negative) {
    // 1. Duplicate element in the stream
    {
        DequeArchive archive;
        archive << static_cast<size_t>(2);  // Expecting 2 elements
        archive << 42 << 42;                // Duplicate key 42

        std::set<int> int_set;
        EXPECT_THAT(ReadValue(archive, int_set), Not(IsOk()));
    }

    // 2. Valid size, but payload cuts off mid-stream
    {
        DequeArchive archive;
        archive << static_cast<size_t>(3);  // Expecting 3 elements
        archive << 10 << 20;                // Only 2 written

        std::set<int> int_set;
        EXPECT_THAT(ReadValue(archive, int_set), Not(IsOk()));
    }

    // 3. Completely empty archive
    {
        DequeArchive empty_archive;
        std::set<int> int_set;
        EXPECT_THAT(ReadValue(empty_archive, int_set), Not(IsOk()));
    }
}

TEST(string, positive) {
    DequeArchive archive;

    const std::string str = "Hello, world!";

    archive << str;
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds(str));
    EXPECT_TRUE(archive.Empty());
}

TEST(string, negative) {
    DequeArchive archive;

    const std::string str = "Hello, world!";

    archive << str;
    ASSERT_FALSE(archive.Empty());
    archive.storage.pop_back();
    EXPECT_THAT(ReadOneValue<std::string>(archive), Not(IsOk()));
}

TEST(vector, pod_positive) {
    DequeArchive archive;

    const std::vector<int> empty_vec;
    const std::vector<int> numbers = {1, 42, 22, 67, 300};

    archive << empty_vec << numbers;

    {
        std::vector<int> read_empty;
        ASSERT_THAT(ReadValue(archive, read_empty), IsOk());
        EXPECT_THAT(read_empty, IsEmpty());
    }
    {
        std::vector<int> read_numbers;
        ASSERT_THAT(ReadValue(archive, read_numbers), IsOk());
        EXPECT_EQ(read_numbers, numbers);
    }
}

// Tests read failure on truncated or invalid byte buffer for POD types
TEST(vector, pod_negative) {
    // 1. Truncated stream when reading elements
    {
        DequeArchive archive;
        const size_t fake_size = 100;
        archive << fake_size;  // Says 100 elements, but no actual data written

        std::vector<int> numbers;
        EXPECT_THAT(ReadValue(archive, numbers), Not(IsOk()));
    }

    // 2. Truncated stream when reading size itself
    {
        DequeArchive empty_archive;
        std::vector<int> numbers;
        EXPECT_THAT(ReadValue(empty_archive, numbers), Not(IsOk()));
    }
}

// Tests the element-by-element fallback path for non-POD types
TEST(vector, complex_positive) {
    DequeArchive archive;

    const std::vector<Person> empty_vec;
    const std::vector<Person> people = {{"Alice", 30}, {"Bob", 25}};

    archive << empty_vec << people;

    {
        std::vector<Person> read_empty;
        ASSERT_THAT(ReadValue(archive, read_empty), IsOk());
        EXPECT_THAT(read_empty, IsEmpty());
    }
    {
        std::vector<Person> read_people;
        ASSERT_THAT(ReadValue(archive, read_people), IsOk());
        EXPECT_THAT(read_people, ElementsAre(Person{"Alice", 30}, Person{"Bob", 25}));
    }
}

// Tests read failure when an individual non-POD element fails to deserialize
TEST(vector, complex_negative) {
    // 1. Valid size, but payload cuts off mid-stream
    {
        DequeArchive archive;
        archive << static_cast<size_t>(2);
        archive << Person{"Alice", 30};  // Only 1 written, 2 expected

        std::vector<Person> people;
        EXPECT_THAT(ReadValue(archive, people), Not(IsOk()));
    }

    // 2. Completely empty archive
    {
        DequeArchive empty_archive;
        std::vector<Person> people;
        EXPECT_THAT(ReadValue(empty_archive, people), Not(IsOk()));
    }
}

}  // namespace goldfish::archive

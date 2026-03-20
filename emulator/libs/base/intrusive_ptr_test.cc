// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/base/intrusive_ptr.h"

#include <gtest/gtest.h>

#include <atomic>

namespace {

// A simple test class with an internal reference count.
class RefCounter {
  public:
    RefCounter() : mRefCount(0) { sLiveInstances++; }
    ~RefCounter() { sLiveInstances--; }

    void addRef() { mRefCount++; }
    void release() {
        if (--mRefCount == 0) {
            delete this;
        }
    }

    int getRefCount() const { return mRefCount; }

    static int getLiveInstances() { return sLiveInstances; }
    static void resetLiveInstances() { sLiveInstances = 0; }

  private:
    std::atomic<int> mRefCount;
    static std::atomic<int> sLiveInstances;
};

std::atomic<int> RefCounter::sLiveInstances(0);

// ADL-discoverable functions for IntrusivePtr.
void intrusive_ptr_add_ref(RefCounter* p) {
    p->addRef();
}

void intrusive_ptr_release(RefCounter* p) {
    p->release();
}

void intrusive_ptr_ctor(RefCounter* p) {
    intrusive_ptr_add_ref(p);  // mRefCount starts from zero
}

using Ptr = goldfish::base::IntrusivePtr<RefCounter>;

class IntrusivePtrTest : public ::testing::Test {
  protected:
    void SetUp() override { RefCounter::resetLiveInstances(); }
    void TearDown() override { EXPECT_EQ(0, RefCounter::getLiveInstances()); }
};

TEST_F(IntrusivePtrTest, DefaultConstructor) {
    Ptr p;
    EXPECT_EQ(nullptr, p.get());
    EXPECT_FALSE(p);
}

TEST_F(IntrusivePtrTest, ConstructorFromRawPointer) {
    auto* raw = new RefCounter();
    EXPECT_EQ(0, raw->getRefCount());

    Ptr p(raw);
    EXPECT_EQ(raw, p.get());
    EXPECT_EQ(1, raw->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, ConstructorFromNullRawPointer) {
    Ptr p(nullptr);
    EXPECT_EQ(nullptr, p.get());
}

TEST_F(IntrusivePtrTest, CopyConstructor) {
    Ptr p1(new RefCounter());
    EXPECT_EQ(1, p1->getRefCount());

    Ptr p2(p1);
    EXPECT_EQ(p1.get(), p2.get());
    EXPECT_EQ(2, p1->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyConstructorFromNull) {
    Ptr p1;
    Ptr p2(p1);
    EXPECT_EQ(nullptr, p1.get());
    EXPECT_EQ(nullptr, p2.get());
}

TEST_F(IntrusivePtrTest, MoveConstructor) {
    auto* raw = new RefCounter();
    Ptr p1(raw);
    EXPECT_EQ(1, raw->getRefCount());

    Ptr p2(std::move(p1));
    EXPECT_EQ(raw, p2.get());
    EXPECT_EQ(nullptr, p1.get());
    EXPECT_EQ(1, raw->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, Destructor) {
    {
        Ptr p1(new RefCounter());
        EXPECT_EQ(1, RefCounter::getLiveInstances());
        {
            Ptr p2(p1);
            EXPECT_EQ(1, RefCounter::getLiveInstances());
        }
        EXPECT_EQ(1, RefCounter::getLiveInstances());
    }
    EXPECT_EQ(0, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignment) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    EXPECT_EQ(1, raw1->getRefCount());
    EXPECT_EQ(1, raw2->getRefCount());
    EXPECT_EQ(2, RefCounter::getLiveInstances());

    p1 = p2;

    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(2, raw2->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentSelf) {
    Ptr p1(new RefCounter());
    auto* raw = p1.get();

    p1 = *&p1;

    EXPECT_EQ(1, raw->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentToNull) {
    Ptr p1;
    Ptr p2(new RefCounter());
    auto* raw2 = p2.get();

    p1 = p2;
    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(2, raw2->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentFromNull) {
    Ptr p1(new RefCounter());
    Ptr p2;

    p1 = p2;
    EXPECT_EQ(nullptr, p1.get());
    EXPECT_EQ(0, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, MoveAssignment) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    p1 = std::move(p2);

    EXPECT_EQ(raw2, p1.get());
    EXPECT_EQ(nullptr, p2.get());
    EXPECT_EQ(1, raw2->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, MoveAssignmentSelf) {
    Ptr p1(new RefCounter());
    auto* raw = p1.get();

    p1 = std::move(*&p1);

    EXPECT_EQ(raw, p1.get());
    EXPECT_EQ(1, raw->getRefCount());
    EXPECT_EQ(1, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, ResetToNull) {
    Ptr p(new RefCounter());
    p.reset();
    EXPECT_EQ(nullptr, p.get());
    EXPECT_EQ(0, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, Swap) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    p1.swap(p2);

    EXPECT_EQ(raw2, p1.get());
    EXPECT_EQ(raw1, p2.get());
    EXPECT_EQ(1, raw1->getRefCount());
    EXPECT_EQ(1, raw2->getRefCount());
    EXPECT_EQ(2, RefCounter::getLiveInstances());
}

TEST_F(IntrusivePtrTest, Get) {
    auto* raw = new RefCounter();
    Ptr p(raw);
    EXPECT_EQ(raw, p.get());
}

TEST_F(IntrusivePtrTest, Dereference) {
    Ptr p(new RefCounter());
    EXPECT_EQ(1, (*p).getRefCount());
}

TEST_F(IntrusivePtrTest, ArrowOperator) {
    Ptr p(new RefCounter());
    EXPECT_EQ(1, p->getRefCount());
}

TEST_F(IntrusivePtrTest, BoolOperator) {
    Ptr p1;
    Ptr p2(new RefCounter());
    EXPECT_FALSE(p1);
    EXPECT_TRUE(p2);
}

TEST_F(IntrusivePtrTest, ComparisonOperators) {
    Ptr p1(new RefCounter());
    Ptr p2(p1);
    Ptr p3(new RefCounter());
    Ptr p_null;

    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 != p2);
    EXPECT_FALSE(p1 == p3);
    EXPECT_TRUE(p1 != p3);

    EXPECT_TRUE(p1 != nullptr);
    EXPECT_TRUE(nullptr != p1);
    EXPECT_FALSE(p1 == nullptr);
    EXPECT_FALSE(nullptr == p1);

    EXPECT_TRUE(p_null == nullptr);
    EXPECT_TRUE(nullptr == p_null);
    EXPECT_FALSE(p_null != nullptr);
    EXPECT_FALSE(nullptr != p_null);

    // Test operator<
    EXPECT_EQ(std::less<RefCounter*>()(p1.get(), p3.get()), p1 < p3);
    EXPECT_EQ(std::less<RefCounter*>()(p3.get(), p1.get()), p3 < p1);
}

}  // namespace

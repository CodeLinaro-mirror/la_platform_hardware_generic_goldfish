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
    RefCounter() : ref_count_(0) { live_instances_++; }
    ~RefCounter() { live_instances_--; }

    void AddRef() { ref_count_++; }
    void Release() {
        if (--ref_count_ == 0) {
            delete this;
        }
    }

    int GetRefCount() const { return ref_count_; }

    static int GetLiveInstances() { return live_instances_; }
    static void ResetLiveInstances() { live_instances_ = 0; }

  private:
    std::atomic<int> ref_count_;
    static std::atomic<int> live_instances_;
};

std::atomic<int> RefCounter::live_instances_(0);

// ADL-discoverable functions for IntrusivePtr.
void IntrusivePtrAddRef(RefCounter* p) {
    p->AddRef();
}

void IntrusivePtrRelease(RefCounter* p) {
    p->Release();
}

void IntrusivePtrCtor(RefCounter* p) {
    IntrusivePtrAddRef(p);  // mRefCount starts from zero
}

using Ptr = goldfish::base::IntrusivePtr<RefCounter>;

class IntrusivePtrTest : public ::testing::Test {
  protected:
    void SetUp() override { RefCounter::ResetLiveInstances(); }
    void TearDown() override { EXPECT_EQ(0, RefCounter::GetLiveInstances()); }
};

TEST_F(IntrusivePtrTest, DefaultConstructor) {
    Ptr p;
    EXPECT_EQ(nullptr, p.get());
    EXPECT_FALSE(p);
}

TEST_F(IntrusivePtrTest, ConstructorFromRawPointer) {
    auto* raw = new RefCounter();
    EXPECT_EQ(0, raw->GetRefCount());

    Ptr p(raw);
    EXPECT_EQ(raw, p.get());
    EXPECT_EQ(1, raw->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, ConstructorFromNullRawPointer) {
    Ptr p(nullptr);
    EXPECT_EQ(nullptr, p.get());
}

TEST_F(IntrusivePtrTest, CopyConstructor) {
    Ptr p1(new RefCounter());
    EXPECT_EQ(1, p1->GetRefCount());

    Ptr p2(p1);
    EXPECT_EQ(p1.get(), p2.get());
    EXPECT_EQ(2, p1->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
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
    EXPECT_EQ(1, raw->GetRefCount());

    Ptr p2(std::move(p1));
    EXPECT_EQ(raw, p2.get());
    EXPECT_EQ(nullptr, p1.get());
    EXPECT_EQ(1, raw->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, Destructor) {
    {
        Ptr p1(new RefCounter());
        EXPECT_EQ(1, RefCounter::GetLiveInstances());
        {
            Ptr p2(p1);
            EXPECT_EQ(1, RefCounter::GetLiveInstances());
        }
        EXPECT_EQ(1, RefCounter::GetLiveInstances());
    }
    EXPECT_EQ(0, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignment) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    EXPECT_EQ(1, raw1->GetRefCount());
    EXPECT_EQ(1, raw2->GetRefCount());
    EXPECT_EQ(2, RefCounter::GetLiveInstances());

    p1 = p2;

    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(2, raw2->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentSelf) {
    Ptr p1(new RefCounter());
    auto* raw = p1.get();

    p1 = *&p1;

    EXPECT_EQ(1, raw->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentToNull) {
    Ptr p1;
    Ptr p2(new RefCounter());
    auto* raw2 = p2.get();

    p1 = p2;
    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(2, raw2->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, CopyAssignmentFromNull) {
    Ptr p1(new RefCounter());
    Ptr p2;

    p1 = p2;
    EXPECT_EQ(nullptr, p1.get());
    EXPECT_EQ(0, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, MoveAssignment) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    p1 = std::move(p2);

    EXPECT_EQ(raw2, p1.get());
    EXPECT_EQ(nullptr, p2.get());
    EXPECT_EQ(1, raw2->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, MoveAssignmentSelf) {
    Ptr p1(new RefCounter());
    auto* raw = p1.get();

    p1 = std::move(*&p1);

    EXPECT_EQ(raw, p1.get());
    EXPECT_EQ(1, raw->GetRefCount());
    EXPECT_EQ(1, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, ResetToNull) {
    Ptr p(new RefCounter());
    p.reset();
    EXPECT_EQ(nullptr, p.get());
    EXPECT_EQ(0, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, Swap) {
    Ptr p1(new RefCounter());
    Ptr p2(new RefCounter());
    auto* raw1 = p1.get();
    auto* raw2 = p2.get();

    p1.swap(p2);

    EXPECT_EQ(raw2, p1.get());
    EXPECT_EQ(raw1, p2.get());
    EXPECT_EQ(1, raw1->GetRefCount());
    EXPECT_EQ(1, raw2->GetRefCount());
    EXPECT_EQ(2, RefCounter::GetLiveInstances());
}

TEST_F(IntrusivePtrTest, Get) {
    auto* raw = new RefCounter();
    Ptr p(raw);
    EXPECT_EQ(raw, p.get());
}

TEST_F(IntrusivePtrTest, Dereference) {
    Ptr p(new RefCounter());
    EXPECT_EQ(1, (*p).GetRefCount());
}

TEST_F(IntrusivePtrTest, ArrowOperator) {
    Ptr p(new RefCounter());
    EXPECT_EQ(1, p->GetRefCount());
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

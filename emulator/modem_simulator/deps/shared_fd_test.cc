/* Copyright (C) 2025 The Android Open Source Project
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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

#include <gtest/gtest.h>

namespace cuttlefish {

TEST(SharedFDTest, Set_IsSet) {
  SharedFD a;
  SharedFD b;
  ASSERT_TRUE(SharedFD::Pipe(&a, &b));

  int max_index_a = 0;
  fd_set fds_a;
  FD_ZERO(&fds_a);

  EXPECT_FALSE(a->IsSet(&fds_a));
  a->Set(&fds_a, &max_index_a);
  EXPECT_GT(max_index_a, 0);
  EXPECT_TRUE(a->IsSet(&fds_a));

  int max_index_b = 0;
  fd_set fds_b;
  FD_ZERO(&fds_b);

  EXPECT_FALSE(b->IsSet(&fds_b));
  b->Set(&fds_b, &max_index_b);
  EXPECT_GT(max_index_b, 0);
  EXPECT_TRUE(b->IsSet(&fds_b));

  EXPECT_FALSE(a->IsSet(&fds_b));
  EXPECT_FALSE(b->IsSet(&fds_a));
  EXPECT_NE(max_index_a, max_index_b);
}

TEST(SharedFDTest, MarkAll) {
  SharedFD a;
  SharedFD b;
  ASSERT_TRUE(SharedFD::Pipe(&a, &b));

  SharedFDSet sfdset;
  sfdset.Set(a);
  sfdset.Set(b);

  int max_index = 0;
  fd_set fds;
  FD_ZERO(&fds);

  impl::MarkAll(sfdset, &fds, &max_index);
  EXPECT_GT(max_index, 0);
  EXPECT_TRUE(a->IsSet(&fds));
  EXPECT_TRUE(b->IsSet(&fds));
}

TEST(SharedFDTest, CheckMarked_empty) {
  SharedFD a;
  SharedFD b;
  ASSERT_TRUE(SharedFD::Pipe(&a, &b));

  SharedFDSet sfdset;
  sfdset.Set(a);
  ASSERT_TRUE(sfdset.IsSet(a));
  sfdset.Set(b);
  ASSERT_TRUE(sfdset.IsSet(b));

  fd_set fds;
  FD_ZERO(&fds);
  impl::CheckMarked(&fds, &sfdset);

  EXPECT_FALSE(sfdset.IsSet(a));
  EXPECT_FALSE(sfdset.IsSet(b));
}

TEST(SharedFDTest, CheckMarked_some) {
  SharedFD a;
  SharedFD b;
  ASSERT_TRUE(SharedFD::Pipe(&a, &b));

  SharedFDSet sfdset;
  sfdset.Set(a);
  ASSERT_TRUE(sfdset.IsSet(a));
  sfdset.Set(b);
  ASSERT_TRUE(sfdset.IsSet(b));

  int max_index = 0;
  fd_set fds;
  FD_ZERO(&fds);

  b->Set(&fds, &max_index);
  EXPECT_GT(max_index, 0);

  impl::CheckMarked(&fds, &sfdset);

  EXPECT_FALSE(sfdset.IsSet(a));
  EXPECT_TRUE(sfdset.IsSet(b));
}

}  // namespace cuttlefish
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "goldfish/blocking_stream_buf.h"
#include "goldfish/synchronized_stream_buf.h"

namespace goldfish {

using ::testing::Eq;

// --- BlockingStreamBuf Tests ---

class BlockingStreamBufTest : public ::testing::Test {
  protected:
    BlockingStreamBuf<char> buf_;
    std::iostream stream_{&buf_};
};

TEST_F(BlockingStreamBufTest, WriteAndReadBasic) {
    // Note the newline! (without it will block on the read)
    stream_ << "Hello World\n";
    stream_.flush();

    std::string output;
    std::getline(stream_, output);

    EXPECT_THAT(output, Eq("Hello World"));
}

TEST_F(BlockingStreamBufTest, PartialRead) {
    stream_ << "123456";
    stream_.flush();

    char buffer[4] = {0};
    stream_.read(buffer, 3);

    EXPECT_THAT(std::string(buffer), Eq("123"));
    EXPECT_THAT(stream_.gcount(), Eq(3));

    char buffer2[4] = {0};
    stream_.read(buffer2, 3);
    EXPECT_THAT(std::string(buffer2), Eq("456"));
}

TEST_F(BlockingStreamBufTest, BlockingRead) {
    auto reader = std::async(std::launch::async, [this]() {
        char c;
        stream_.get(c);
        return c;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    stream_ << 'X';
    stream_.flush();

    EXPECT_THAT(reader.get(), Eq('X'));
}

TEST_F(BlockingStreamBufTest, CloseUnblocksReaderWithEOF) {
    auto reader = std::async(std::launch::async, [this]() {
        char c;
        stream_.get(c);
        return stream_.eof();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    buf_.Close();

    EXPECT_TRUE(reader.get());
}

TEST_F(BlockingStreamBufTest, ReadDataThenEOFAfterClose) {
    stream_ << "Data";
    stream_.flush();

    buf_.Close();

    std::string out;
    stream_ >> out;

    EXPECT_THAT(out, Eq("Data"));

    char c;
    stream_.get(c);
    EXPECT_TRUE(stream_.eof());
}

// --- SynchronizedStreamBuf Tests ---

TEST(SynchronizedStreamBufTest, BasicWrite) {
    std::stringbuf inner;
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::ostream os(&sync_buf);

    // Note, here we do not need a newline
    os << "Hello World";
    os.flush();

    EXPECT_THAT(inner.str(), Eq("Hello World"));
}

TEST(SynchronizedStreamBufTest, BasicRead) {
    std::stringbuf inner("Input Data");
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::istream is(&sync_buf);

    std::string out;
    is >> out;
    EXPECT_THAT(out, Eq("Input"));
    is >> out;
    EXPECT_THAT(out, Eq("Data"));
}

TEST(SynchronizedStreamBufTest, SyncDelegation) {
    class MockBuf : public std::stringbuf {
      public:
        int sync() override {
            sync_called = true;
            return std::stringbuf::sync();
        }
        bool sync_called = false;
    };

    MockBuf inner;
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::ostream os(&sync_buf);

    os << "data";
    os.flush();

    EXPECT_TRUE(inner.sync_called);
}

TEST(SynchronizedStreamBufTest, ConcurrentWrite) {
    std::stringbuf inner;
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::ostream os(&sync_buf);

    const int kNumThreads = 4;
    const int kWritesPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&os, i]() {
            for (int j = 0; j < kWritesPerThread; ++j) {
                std::string msg = "T" + std::to_string(i) + " ";
                os.write(msg.data(), msg.size());
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_THAT(inner.str().size(), Eq(kNumThreads * kWritesPerThread * 3));
}

}  // namespace goldfish

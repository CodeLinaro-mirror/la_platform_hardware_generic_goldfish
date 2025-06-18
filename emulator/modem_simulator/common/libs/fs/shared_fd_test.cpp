/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "shared_fd.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"

#include "aemu/base/sockets/SocketUtils.h"
#include "shared_select.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cuttlefish {

// Test fixture for SharedFD tests
class SharedFDTest : public ::testing::Test {};

// Test FileInstance basic functionality
TEST_F(SharedFDTest, FileInstanceBasic) {
    // Test closed instance
    auto closed_instance = FileInstance::ClosedInstance();
    EXPECT_FALSE(closed_instance->IsOpen());
    EXPECT_EQ(closed_instance->getFd(), -1);

    // Test valid file descriptor
    int test_fd = 42;
    FileInstance instance(test_fd, 0);
    EXPECT_TRUE(instance.IsOpen());
    EXPECT_EQ(instance.getFd(), test_fd);

    // Test close functionality
    instance.Close();
    EXPECT_FALSE(instance.IsOpen());
    EXPECT_EQ(instance.getFd(), -1);
}

// Test FileInstance constructor with error
TEST_F(SharedFDTest, FileInstanceConstructorWithError) {
    int test_fd = -1;
    int test_errno = EBADF;
    FileInstance instance(test_fd, test_errno);
    EXPECT_FALSE(instance.IsOpen());
    EXPECT_EQ(instance.getFd(), test_fd);
}

// Test SharedFD default constructor
TEST_F(SharedFDTest, SharedFDDefaultConstructor) {
    SharedFD fd;
    EXPECT_FALSE(fd->IsOpen());
    EXPECT_EQ(fd.getFd(), -1);
}

// Test SharedFD constructor with FileInstance
TEST_F(SharedFDTest, SharedFDConstructorWithFileInstance) {
    int test_fd = 42;
    auto file_instance = std::make_shared<FileInstance>(test_fd, 0);
    SharedFD fd(file_instance);
    EXPECT_TRUE(fd->IsOpen());
    EXPECT_EQ(fd.getFd(), test_fd);
}

// Test SharedFD dereference operators
TEST_F(SharedFDTest, SharedFDDereferenceOperators) {
    int test_fd = 42;
    auto file_instance = std::make_shared<FileInstance>(test_fd, 0);
    SharedFD fd(file_instance);

    // Test arrow operator
    EXPECT_TRUE(fd->IsOpen());
    EXPECT_EQ(fd->getFd(), test_fd);

    // Test dereference operator
    EXPECT_TRUE((*fd).IsOpen());
    EXPECT_EQ((*fd).getFd(), test_fd);
}

// Test pipe creation
TEST_F(SharedFDTest, PipeCreation) {
    SharedFD fd0, fd1;
    bool success = SharedFD::Pipe(&fd0, &fd1);

    EXPECT_TRUE(success);
    EXPECT_TRUE(fd0->IsOpen());
    EXPECT_TRUE(fd1->IsOpen());
    EXPECT_NE(fd0.getFd(), fd1.getFd());
}

// Test pipe communication
TEST_F(SharedFDTest, PipeCommunication) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));
    // The pipe is non-blocking by default.
    ::android::base::socketSetBlocking(fd0.getFd());
    ::android::base::socketSetBlocking(fd1.getFd());

    std::string test_message = "Hello, pipe!";
    std::string received_message;
    received_message.resize(test_message.size());

    // Write to one end
    ssize_t written = fd1->Write(test_message.data(), test_message.size());
    EXPECT_EQ(written, static_cast<ssize_t>(test_message.size()));

    // Read from the other end
    ssize_t read = fd0->Read(&received_message[0], test_message.size());
    EXPECT_EQ(read, static_cast<ssize_t>(test_message.size()));
    EXPECT_EQ(received_message, test_message);
}

// Test WriteAll functionality
TEST_F(SharedFDTest, WriteAll) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));
    // The pipe is non-blocking by default.
    ::android::base::socketSetBlocking(fd0.getFd());
    ::android::base::socketSetBlocking(fd1.getFd());

    std::string test_message = "Testing WriteAll functionality";

    // Test WriteAll
    ssize_t written = fd1.WriteAll(test_message);
    EXPECT_EQ(written, static_cast<ssize_t>(test_message.size()));

    // Verify data was written
    std::string received_message;
    received_message.resize(test_message.size());
    ssize_t read = fd0->Read(&received_message[0], test_message.size());
    EXPECT_EQ(read, static_cast<ssize_t>(test_message.size()));
    EXPECT_EQ(received_message, test_message);
}

// Test ReadExact functionality
TEST_F(SharedFDTest, ReadExact) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));
    // The pipe is non-blocking by default.
    ::android::base::socketSetBlocking(fd0.getFd());
    ::android::base::socketSetBlocking(fd1.getFd());

    std::string test_message = "Testing ReadExact functionality";

    // Write data
    fd1->Write(test_message.data(), test_message.size());

    // Test ReadExact
    std::string received_message;
    received_message.resize(test_message.size());
    ssize_t read = fd0.ReadExact(received_message);
    EXPECT_EQ(read, static_cast<ssize_t>(test_message.size()));
    EXPECT_EQ(received_message, test_message);
}

// Test socket server creation
TEST_F(SharedFDTest, SocketLocalServer) {
    int test_port = 12345;
    SharedFD server = SharedFD::SocketLocalServer(test_port);

    // Note: This might fail if port is already in use, which is expected
    if (server->IsOpen()) {
        EXPECT_GT(server.getFd(), 0);
        // Test isIpv4 flag
        // The implementation sets isIpv4_ to true if IPv6 fails and IPv4 succeeds
        // We can't easily test this without knowing the network configuration
    }
}

// Test socket client creation
TEST_F(SharedFDTest, SocketLocalClient) {
    int test_port = 12346;

    // Start a server first
    SharedFD server = SharedFD::SocketLocalServer(test_port);
    if (!server->IsOpen()) {
        GTEST_SKIP() << "Could not create server socket, skipping client test";
    }

    // Try to create a client
    SharedFD client = SharedFD::SocketLocalClient(test_port);
    EXPECT_TRUE(client->IsOpen());
}

// Test socket client with invalid name
TEST_F(SharedFDTest, SocketLocalClientWithInvalidName) {
    std::string invalid_name = "invalid_name";
    bool abstract = true;
    int type = SOCK_STREAM;

    SharedFD client = SharedFD::SocketLocalClient(invalid_name, abstract, type);
    EXPECT_FALSE(client->IsOpen());
}

// Test socket client with wrong type
TEST_F(SharedFDTest, SocketLocalClientWithWrongType) {
    std::string test_name = "modem_simulator1234";
    bool abstract = true;
    int type = SOCK_DGRAM;  // Wrong type

    SharedFD client = SharedFD::SocketLocalClient(test_name, abstract, type);
    EXPECT_FALSE(client->IsOpen());
}

// Test socket client with non-abstract
TEST_F(SharedFDTest, SocketLocalClientWithNonAbstract) {
    std::string test_name = "modem_simulator1234";
    bool abstract = false;  // Non-abstract
    int type = SOCK_STREAM;

    SharedFD client = SharedFD::SocketLocalClient(test_name, abstract, type);
    EXPECT_FALSE(client->IsOpen());
}

// Test SharedFDSet basic functionality
TEST_F(SharedFDTest, SharedFDSetBasic) {
    SharedFDSet fd_set;

    // Test initial state
    EXPECT_EQ(fd_set.Size(), 0);

    // Create some test FDs
    SharedFD fd1, fd2, fd3;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));
    ASSERT_TRUE(SharedFD::Pipe(&fd3, &fd2));

    // Test Set and IsSet
    fd_set.Set(fd1);
    EXPECT_EQ(fd_set.Size(), 1);
    EXPECT_TRUE(fd_set.IsSet(fd1));
    EXPECT_FALSE(fd_set.IsSet(fd2));

    fd_set.Set(fd2);
    EXPECT_EQ(fd_set.Size(), 2);
    EXPECT_TRUE(fd_set.IsSet(fd1));
    EXPECT_TRUE(fd_set.IsSet(fd2));

    // Test Clr
    fd_set.Clr(fd1);
    EXPECT_EQ(fd_set.Size(), 1);
    EXPECT_FALSE(fd_set.IsSet(fd1));
    EXPECT_TRUE(fd_set.IsSet(fd2));

    // Test Zero
    fd_set.Zero();
    EXPECT_EQ(fd_set.Size(), 0);
    EXPECT_FALSE(fd_set.IsSet(fd1));
    EXPECT_FALSE(fd_set.IsSet(fd2));
}

// Test SharedFDSet iteration
TEST_F(SharedFDTest, SharedFDSetIteration) {
    SharedFDSet fd_set;

    SharedFD fd1, fd2, fd3;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));
    ASSERT_TRUE(SharedFD::Pipe(&fd3, &fd2));

    fd_set.Set(fd1);
    fd_set.Set(fd2);
    fd_set.Set(fd3);

    EXPECT_EQ(fd_set.Size(), 3);

    // Test iteration
    int count = 0;
    for (const auto& fd : fd_set) {
        EXPECT_TRUE(fd->IsOpen());
        count++;
    }
    EXPECT_EQ(count, 3);
}

// Test SharedFDSet swap
TEST_F(SharedFDTest, SharedFDSetSwap) {
    SharedFDSet fd_set1, fd_set2;

    SharedFD fd1, fd2;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));

    fd_set1.Set(fd1);
    fd_set2.Set(fd2);

    EXPECT_EQ(fd_set1.Size(), 1);
    EXPECT_EQ(fd_set2.Size(), 1);
    EXPECT_TRUE(fd_set1.IsSet(fd1));
    EXPECT_TRUE(fd_set2.IsSet(fd2));

    fd_set1.swap(&fd_set2);

    EXPECT_EQ(fd_set1.Size(), 1);
    EXPECT_EQ(fd_set2.Size(), 1);
    EXPECT_TRUE(fd_set1.IsSet(fd2));
    EXPECT_TRUE(fd_set2.IsSet(fd1));
}

// Test Select functionality with timeout
TEST_F(SharedFDTest, SelectWithTimeout) {
    SharedFDSet read_set;

    // Create a pipe for testing
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    read_set.Set(fd0);

    // Test with immediate timeout
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;  // 1ms timeout

    int result = Select(&read_set, nullptr, nullptr, &timeout);
    EXPECT_GE(result, 0);  // Should not fail

    // Test with no timeout (blocking)
    read_set.Set(fd0);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    // Start a thread to write to the pipe after a short delay
    std::thread writer([&fd1]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fd1->Write("test", 4);
    });

    result = Select(&read_set, nullptr, nullptr, &timeout);
    writer.join();

    EXPECT_GE(result, 0);  // Should not fail
}

// Test Select with data ready
TEST_F(SharedFDTest, SelectWithDataReady) {
    SharedFDSet read_set;

    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    read_set.Set(fd0);

    // Write data to make it ready for reading
    fd1->Write("test", 4);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;  // 1ms timeout

    int result = Select(&read_set, nullptr, nullptr, &timeout);
    EXPECT_GT(result, 0);  // Should indicate ready FDs

    // Verify the FD is still in the set
    EXPECT_TRUE(read_set.IsSet(fd0));
}

// Test Select with null read_set
TEST_F(SharedFDTest, SelectWithNullReadSet) {
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    int result = Select(nullptr, nullptr, nullptr, &timeout);
    EXPECT_EQ(result, -1);  // Should fail with null read_set
}

// Test FileInstance Set and IsSet with fd_set
TEST_F(SharedFDTest, FileInstanceSetAndIsSet) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    fd_set test_fd_set;
    FD_ZERO(&test_fd_set);
    int max_index = 0;

    // Test Set
    fd0->Set(&test_fd_set, &max_index);
    EXPECT_GT(max_index, 0);
    EXPECT_TRUE(FD_ISSET(fd0.getFd(), &test_fd_set));

    // Test IsSet
    EXPECT_TRUE(fd0->IsSet(&test_fd_set));
    EXPECT_FALSE(fd1->IsSet(&test_fd_set));
}

// Test FileInstance Set with closed FD
TEST_F(SharedFDTest, FileInstanceSetWithClosedFD) {
    auto closed_instance = FileInstance::ClosedInstance();

    fd_set test_fd_set;
    FD_ZERO(&test_fd_set);
    int max_index = 0;

    closed_instance->Set(&test_fd_set, &max_index);
    EXPECT_EQ(max_index, 0);  // Should not change max_index for closed FD
}

// Test FileInstance IsSet with closed FD
TEST_F(SharedFDTest, FileInstanceIsSetWithClosedFD) {
    auto closed_instance = FileInstance::ClosedInstance();

    fd_set test_fd_set;
    FD_ZERO(&test_fd_set);

    EXPECT_FALSE(closed_instance->IsSet(&test_fd_set));
}

// Test error handling in Read/Write
TEST_F(SharedFDTest, ReadWriteErrorHandling) {
    auto closed_instance = FileInstance::ClosedInstance();

    char buffer[10];

    // Test Read on closed FD
    ssize_t read_result = closed_instance->Read(buffer, sizeof(buffer));
    EXPECT_EQ(read_result, -1);

    // Test Write on closed FD
    ssize_t write_result = closed_instance->Write(buffer, sizeof(buffer));
    EXPECT_EQ(write_result, -1);
}

// Test WriteAll with empty string
TEST_F(SharedFDTest, WriteAllEmptyString) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    std::string empty_string;
    ssize_t written = fd1.WriteAll(empty_string);
    EXPECT_EQ(written, 0);
}

// Test ReadExact with empty buffer
TEST_F(SharedFDTest, ReadExactEmptyBuffer) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    std::string empty_buffer;
    ssize_t read = fd0.ReadExact(empty_buffer);
    EXPECT_EQ(read, 0);
}

// Test multiple pipes communication
TEST_F(SharedFDTest, MultiplePipesCommunication) {
    SharedFD pipe1_read, pipe1_write;
    SharedFD pipe2_read, pipe2_write;

    ASSERT_TRUE(SharedFD::Pipe(&pipe1_read, &pipe1_write));
    ASSERT_TRUE(SharedFD::Pipe(&pipe2_read, &pipe2_write));

    std::string message1 = "Message from pipe 1";
    std::string message2 = "Message from pipe 2";

    // Write to both pipes
    pipe1_write.WriteAll(message1);
    pipe2_write.WriteAll(message2);

    // Read from both pipes
    std::string received1, received2;
    received1.resize(message1.size());
    received2.resize(message2.size());

    pipe1_read.ReadExact(received1);
    pipe2_read.ReadExact(received2);

    EXPECT_EQ(received1, message1);
    EXPECT_EQ(received2, message2);
}

// Test SharedFDSet with multiple FDs
TEST_F(SharedFDTest, SharedFDSetMultipleFDs) {
    SharedFDSet fd_set;

    SharedFD pipe1_read, pipe1_write;
    SharedFD pipe2_read, pipe2_write;
    SharedFD pipe3_read, pipe3_write;

    ASSERT_TRUE(SharedFD::Pipe(&pipe1_read, &pipe1_write));
    ASSERT_TRUE(SharedFD::Pipe(&pipe2_read, &pipe2_write));
    ASSERT_TRUE(SharedFD::Pipe(&pipe3_read, &pipe3_write));

    fd_set.Set(pipe1_read);
    fd_set.Set(pipe2_read);
    fd_set.Set(pipe3_read);

    EXPECT_EQ(fd_set.Size(), 3);

    // Write to all pipes
    pipe1_write.WriteAll("data1");
    pipe2_write.WriteAll("data2");
    pipe3_write.WriteAll("data3");

    // Test Select with multiple ready FDs
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    int result = Select(&fd_set, nullptr, nullptr, &timeout);
    EXPECT_GT(result, 0);
    EXPECT_LE(result, 3);
}

// Test SharedFDSet duplicate handling
TEST_F(SharedFDTest, SharedFDSetDuplicateHandling) {
    SharedFDSet fd_set;

    SharedFD fd1, fd2;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));

    // Add the same FD multiple times
    fd_set.Set(fd1);
    fd_set.Set(fd1);
    fd_set.Set(fd1);

    // Should only be counted once
    EXPECT_EQ(fd_set.Size(), 1);
    EXPECT_TRUE(fd_set.IsSet(fd1));
}

// Test SharedFDSet remove non-existent FD
TEST_F(SharedFDTest, SharedFDSetRemoveNonExistent) {
    SharedFDSet fd_set;

    SharedFD fd1, fd2;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));

    fd_set.Set(fd1);
    EXPECT_EQ(fd_set.Size(), 1);

    // Remove non-existent FD
    fd_set.Clr(fd2);
    EXPECT_EQ(fd_set.Size(), 1);  // Should remain unchanged
    EXPECT_TRUE(fd_set.IsSet(fd1));
}

// Test SharedFDSet clear and reuse
TEST_F(SharedFDTest, SharedFDSetClearAndReuse) {
    SharedFDSet fd_set;

    SharedFD fd1, fd2;
    ASSERT_TRUE(SharedFD::Pipe(&fd1, &fd2));

    fd_set.Set(fd1);
    EXPECT_EQ(fd_set.Size(), 1);

    fd_set.Zero();
    EXPECT_EQ(fd_set.Size(), 0);

    // Reuse the set
    fd_set.Set(fd2);
    EXPECT_EQ(fd_set.Size(), 1);
    EXPECT_TRUE(fd_set.IsSet(fd2));
}

// Test Accept functionality
TEST_F(SharedFDTest, AcceptFunctionality) {
    // Create a server socket
    int test_port = 12347;
    SharedFD server = SharedFD::SocketLocalServer(test_port);

    if (!server->IsOpen()) {
        GTEST_SKIP() << "Could not create server socket, skipping accept test";
    }

    // Test Accept with null parameters
    // Run accept in a separate thread to avoid blocking the test.
    auto future = std::async(std::launch::async, [&]() { return SharedFD::Accept(*server); });

    // Give the server a moment to start listening before the client connects.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect a client.
    SharedFD connecting_client = SharedFD::SocketLocalClient(test_port);
    ASSERT_TRUE(connecting_client->IsOpen()) << "Client failed to connect.";

    // Wait for the accept to complete, with a timeout.
    auto status = future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "Accept timed out.";

    SharedFD accepted_client = future.get();
    ASSERT_TRUE(accepted_client->IsOpen());

    // Verify the connection by sending and receiving data.
    std::string test_message = "hello";
    ASSERT_EQ(connecting_client->Write(test_message.data(), test_message.size()),
              static_cast<ssize_t>(test_message.size()));
    char buffer[16];
    ssize_t read = accepted_client->Read(buffer, sizeof(buffer));
    ASSERT_EQ(read, static_cast<ssize_t>(test_message.size()));
    EXPECT_EQ(std::string(buffer, read), test_message);
}

// Test Connect functionality
TEST_F(SharedFDTest, ConnectFunctionality) {
    // This test would require a real server to connect to
    // For now, we'll just test that the method exists and doesn't crash
    auto closed_instance = FileInstance::ClosedInstance();

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = closed_instance->Connect((struct sockaddr*)&addr, sizeof(addr));
    EXPECT_EQ(result, -1);  // Should fail for closed FD
}

// Test large data transfer
// Test large data transfer
TEST_F(SharedFDTest, LargeDataTransfer) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    // The pipe is non-blocking by default. For this test, we make them
    // blocking to ensure WriteAll doesn't fail with EAGAIN and instead
    // waits for the reader to make space.
    ::android::base::socketSetBlocking(fd0.getFd());
    ::android::base::socketSetBlocking(fd1.getFd());

    // Create a large message
    const std::string large_message(1024 * 1024, 'A');  // 1MB of data

    // Run writer and reader in separate threads to prevent deadlocking if the
    // pipe buffer is smaller than the message.
    auto writer_future = std::async(
            std::launch::async, [&fd1, &large_message]() { return fd1.WriteAll(large_message); });

    // Read large data
    std::string received_message;
    auto reader_future = std::async(std::launch::async, [&fd0, size = large_message.size()]() {
        std::string received;
        received.resize(size);
        // ReadExact will block until all `size` bytes are read.
        fd0.ReadExact(received);
        return received;
    });

    // Wait for both to complete, with a timeout.
    const auto timeout = std::chrono::seconds(10);

    ASSERT_EQ(writer_future.wait_for(timeout), std::future_status::ready)
            << "Writer timed out after 10 seconds.";
    ASSERT_EQ(reader_future.wait_for(timeout), std::future_status::ready)
            << "Reader timed out after 10 seconds.";

    // Check results
    EXPECT_TRUE(writer_future.get());
    EXPECT_EQ(reader_future.get(), large_message);
}

// Test concurrent access, i.e. make sure we send all strings in one block.
TEST_F(SharedFDTest, ConcurrentAccess) {
    SharedFD fd0, fd1;
    ASSERT_TRUE(SharedFD::Pipe(&fd0, &fd1));

    // The pipe is non-blocking by default. For this test, we make them
    // blocking to ensure WriteAll doesn't fail with EAGAIN and instead
    // waits for the reader to make space.
    ::android::base::socketSetBlocking(fd0.getFd());
    ::android::base::socketSetBlocking(fd1.getFd());

    const int num_threads = 10;
    const int messages_per_thread = 100;
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;

    // Start writer threads
    for (int i = 0; i < num_threads; ++i) {
        writers.emplace_back([&fd1, i]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                std::string msg =
                        "Thread " + std::to_string(i) + " Message " + std::to_string(100 + j);
                fd1.WriteAll(msg);
            }
        });
    }

    // Start reader threads
    for (int i = 0; i < num_threads; ++i) {
        readers.emplace_back([&fd0]() {
            const std::string expected_template = "Thread . Message ...";
            for (int j = 0; j < messages_per_thread; ++j) {
                std::string received;
                received.resize(expected_template.size());
                fd0.ReadExact(received);
                EXPECT_THAT(received, ::testing::MatchesRegex(expected_template));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& writer : writers) {
        writer.join();
    }
    for (auto& reader : readers) {
        reader.join();
    }
}

}  // namespace cuttlefish
#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

#include "goldfish/device_registry/DeviceProperties.h"
#include "goldfish/device_registry/DeviceRegistry.h"

namespace goldfish {

// Test basic write and read operations.
TEST(DeviceRegistryTest, SetAndGet) {
    auto registry = DeviceRegistry::testRegistry();
    ASSERT_TRUE(registry->setOnce(properties::kAdbPort, 1234));

    auto value = registry->get(properties::kAdbPort);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(1234, value.value());
}

// Test that getting a non-existent key returns an empty optional.
TEST(DeviceRegistryTest, GetNonExistent) {
    auto registry = DeviceRegistry::testRegistry();
    auto value = registry->get(properties::kAdbPort);
    EXPECT_FALSE(value.has_value());
}

// Test the "write-once" policy.
TEST(DeviceRegistryTest, SetOnce) {
    auto registry = DeviceRegistry::testRegistry();
    ASSERT_TRUE(registry->setOnce(properties::kAdbPort, 1234));
    EXPECT_FALSE(registry->setOnce(properties::kAdbPort, 5678));

    auto value = registry->get(properties::kAdbPort);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(1234, value.value());
}

// Test thread safety of set and get operations.
TEST(DeviceRegistryTest, SetAndGetIsThreadSafe) {
    auto registry = DeviceRegistry::testRegistry();

    // Writer threads
    std::vector<std::future<bool>> writer_futures;
    const int num_writers = 100;
    for (int i = 0; i < num_writers; ++i) {
        writer_futures.push_back(std::async(std::launch::async, [&]() {
            return registry->setOnce(properties::kAdbPort, 5555);
        }));
    }

    int success_count = 0;
    for (auto& fut : writer_futures) {
        if (fut.get()) {
            success_count++;
        }
    }
    EXPECT_EQ(1, success_count);

    // Reader threads
    std::vector<std::future<int>> reader_futures;
    const int num_readers = 100;
    for (int i = 0; i < num_readers; ++i) {
        reader_futures.push_back(std::async(std::launch::async, [&]() {
            std::optional<int> value;
            // Spin until the value is set.
            while (!(value = registry->get(properties::kAdbPort))) {
                std::this_thread::yield();
            }
            return value.value();
        }));
    }

    for (auto& fut : reader_futures) {
        EXPECT_EQ(5555, fut.get());
    }
}

}  // namespace goldfish

#include <gtest/gtest.h>

#include <vector>

#include "FakeMultiDisplay.h"
#include "FakePixmanDisplay.h"

namespace android::goldfish {

class FakeMultiDisplayTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Clear all displays except the default one before each test
        reinterpret_cast<FakeMultiDisplay*>(FakeMultiDisplay::instance())->clear();
    }
};

TEST_F(FakeMultiDisplayTest, CreateAndGetDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(1, 800, 600);
    ASSERT_TRUE(result.ok());
    DisplayPtr display = result.value();

    // Get the display by ID
    auto getResult = multiDisplay->getDisplay(1);
    ASSERT_TRUE(getResult.ok());
    DisplayPtr retrievedDisplay = getResult.value();

    // Check if the retrieved display is the same as the created one
    ASSERT_EQ(display.lock().get(), retrievedDisplay.lock().get());
}

TEST_F(FakeMultiDisplayTest, CreateDisplayAlreadyExists) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(2, 800, 600);
    ASSERT_TRUE(result.ok());

    // Try to create a display with the same ID
    auto result2 = multiDisplay->createDisplay(2, 1024, 768);
    ASSERT_FALSE(result2.ok());
    ASSERT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, GetDisplayNotFound) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Try to get a non-existent display
    auto result = multiDisplay->getDisplay(99);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Create a new display
    auto result = multiDisplay->createDisplay(3, 800, 600);
    ASSERT_TRUE(result.ok());

    // Erase the display
    auto eraseResult = multiDisplay->eraseDisplay(3);
    ASSERT_TRUE(eraseResult.ok());

    // Try to get the erased display
    auto getResult = multiDisplay->getDisplay(3);
    ASSERT_FALSE(getResult.ok());
    ASSERT_EQ(getResult.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(FakeMultiDisplayTest, EraseDefaultDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Try to erase the default display
    auto result = multiDisplay->eraseDisplay(0);
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(FakeMultiDisplayTest, Displays) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Create a few displays
    auto result1 = multiDisplay->createDisplay(4, 800, 600);
    ASSERT_TRUE(result1.ok());
    auto result2 = multiDisplay->createDisplay(5, 1024, 768);
    ASSERT_TRUE(result2.ok());

    // Get all displays
    std::vector<DisplayPtr> displays = multiDisplay->displays();

    // Check if the number of displays is correct (including the default display)
    ASSERT_EQ(displays.size(), 3);

    // Check if the default display is present
    bool defaultDisplayFound = false;
    for (const auto& display : displays) {
        if (display.lock()->id() == 0) {
            defaultDisplayFound = true;
            break;
        }
    }
    ASSERT_TRUE(defaultDisplayFound);
}

TEST_F(FakeMultiDisplayTest, DefaultDisplay) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Get the default display
    DisplayPtr defaultDisplay = multiDisplay->defaultDisplay().value();

    // Check if the default display has the correct ID
    ASSERT_EQ(defaultDisplay.lock()->id(), 0);
}

TEST_F(FakeMultiDisplayTest, IsEnabled) {
    // Get the singleton instance
    IMultiDisplay* multiDisplay = FakeMultiDisplay::instance();

    // Check if the display is enabled
    ASSERT_TRUE(multiDisplay->isEnabled());
}

}  // namespace android::goldfish

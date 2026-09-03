#include <gtest/gtest.h>

#include <vector>

#include "android/emulation/control/keyboard/key_conversion.h"
#include "dom_key.h"

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

// Function to check if a QKeyCode is valid
bool IsValidQKeyCode(QKeyCode code) {
    // This is a simple check based on the range of valid QKeyCode values.
    // You might need to adjust this based on the actual QKeyCode definition.
    return code > 0 && code <= Q_KEY_CODE__MAX;
}

TEST(KeyConversionTest, DISABLED_EvdevToQKeyCode) {
    // We do not have a complete mapping. This test is mainly here
    // to point out that some of our defined key codes do not have
    // a corresponding QKeyCode. If these codes are crucial for correct
    // functioning of android we will have to update
    // external/keycodemapdb/data/keymaps.csv

    // Get the keymap
    const std::vector<KeycodeMapEntry>& map = keymap();
    int idx = 0;
    // Iterate through all entries in the keymap
    for (const auto& entry : map) {
        // Check if there is a valid evdev code
        if (entry.evdev != 0) {
            idx++;
            // Translate the evdev code to QKeyCode
            QKeyCode qcode = evdev_to_qcode(entry.evdev);

            // Check if the QKeyCode is valid
            EXPECT_TRUE(IsValidQKeyCode(qcode))
                    << "Invalid QKeyCode " << static_cast<int>(qcode) << " for usb code 0x"
                    << std::hex << entry.usb << std::dec << " for evdev code 0x" << std::hex
                    << entry.evdev << std::dec << " (" << entry.code << ") at index " << idx;
        }
    }
}

TEST(KeyConversionTest, HandlesEvdevBit11) {
    // 102 is KEY_HOME in evdev.
    // 1126 is 102 | 0x400 (bit 11 set).
    uint32_t evdev_102 = keycode_to_evdev(102, KeyCodeType::evdev);
    uint32_t evdev_1126 = keycode_to_evdev(1126, KeyCodeType::evdev);

    EXPECT_EQ(evdev_102, 102U);
    EXPECT_EQ(evdev_102, evdev_1126);
}

TEST(KeyConversionTest, AsciiToQcodeLowercase) {
    auto down_events = ascii_to_qcode('e', true);
    ASSERT_EQ(down_events.size(), 1);
    EXPECT_EQ(down_events[0].code, Q_KEY_CODE_E);
    EXPECT_TRUE(down_events[0].down);
    EXPECT_TRUE(IsValidQKeyCode(down_events[0].code));

    auto up_events = ascii_to_qcode('e', false);
    ASSERT_EQ(up_events.size(), 1);
    EXPECT_EQ(up_events[0].code, Q_KEY_CODE_E);
    EXPECT_FALSE(up_events[0].down);
    EXPECT_TRUE(IsValidQKeyCode(up_events[0].code));
}

TEST(KeyConversionTest, AsciiToQcodeUppercaseLetters) {
    // 'E' requires Shift + E
    auto down_events = ascii_to_qcode('E', true);
    ASSERT_EQ(down_events.size(), 2);
    EXPECT_EQ(down_events[0].code, Q_KEY_CODE_SHIFT);
    EXPECT_TRUE(down_events[0].down);
    EXPECT_EQ(down_events[1].code, Q_KEY_CODE_E);
    EXPECT_TRUE(down_events[1].down);
    for (const auto& ev : down_events) {
        EXPECT_TRUE(IsValidQKeyCode(ev.code));
    }

    auto up_events = ascii_to_qcode('E', false);
    ASSERT_EQ(up_events.size(), 2);
    EXPECT_EQ(up_events[0].code, Q_KEY_CODE_E);
    EXPECT_FALSE(up_events[0].down);
    EXPECT_EQ(up_events[1].code, Q_KEY_CODE_SHIFT);
    EXPECT_FALSE(up_events[1].down);
    for (const auto& ev : up_events) {
        EXPECT_TRUE(IsValidQKeyCode(ev.code));
    }
}

TEST(KeyConversionTest, AsciiToQcodeFnKey) {
    // 'a' has fn = 0xe1 -> Alt + A
    auto down_events = ascii_to_qcode(0xe1, true);
    ASSERT_EQ(down_events.size(), 2);
    EXPECT_EQ(down_events[0].code, Q_KEY_CODE_ALT);
    EXPECT_TRUE(down_events[0].down);
    EXPECT_EQ(down_events[1].code, Q_KEY_CODE_A);
    EXPECT_TRUE(down_events[1].down);

    auto up_events = ascii_to_qcode(0xe1, false);
    ASSERT_EQ(up_events.size(), 2);
    EXPECT_EQ(up_events[0].code, Q_KEY_CODE_A);
    EXPECT_FALSE(up_events[0].down);
    EXPECT_EQ(up_events[1].code, Q_KEY_CODE_ALT);
    EXPECT_FALSE(up_events[1].down);
}

TEST(KeyConversionTest, AsciiToQcodeCapsFnKey) {
    // 'a' has caps_fn = 0xc1 -> Shift + Alt + A
    auto down_events = ascii_to_qcode(0xc1, true);
    ASSERT_EQ(down_events.size(), 3);
    EXPECT_EQ(down_events[0].code, Q_KEY_CODE_SHIFT);
    EXPECT_TRUE(down_events[0].down);
    EXPECT_EQ(down_events[1].code, Q_KEY_CODE_ALT);
    EXPECT_TRUE(down_events[1].down);
    EXPECT_EQ(down_events[2].code, Q_KEY_CODE_A);
    EXPECT_TRUE(down_events[2].down);

    auto up_events = ascii_to_qcode(0xc1, false);
    ASSERT_EQ(up_events.size(), 3);
    EXPECT_EQ(up_events[0].code, Q_KEY_CODE_A);
    EXPECT_FALSE(up_events[0].down);
    EXPECT_EQ(up_events[1].code, Q_KEY_CODE_ALT);
    EXPECT_FALSE(up_events[1].down);
    EXPECT_EQ(up_events[2].code, Q_KEY_CODE_SHIFT);
    EXPECT_FALSE(up_events[2].down);
}

TEST(KeyConversionTest, AsciiToQcodeUnmappedReturnsEmpty) {
    auto events = ascii_to_qcode(0xFFFF, true);
    EXPECT_TRUE(events.empty());
}

TEST(KeyConversionTest, AsciiToQcodeAllAlphabetLettersValid) {
    for (char c = 'A'; c <= 'Z'; ++c) {
        auto events = ascii_to_qcode(c, true);
        ASSERT_EQ(events.size(), 2) << "Failed for uppercase letter " << c;
        EXPECT_EQ(events[0].code, Q_KEY_CODE_SHIFT);
        EXPECT_TRUE(IsValidQKeyCode(events[1].code));
    }
    for (char c = 'a'; c <= 'z'; ++c) {
        auto events = ascii_to_qcode(c, true);
        ASSERT_EQ(events.size(), 1) << "Failed for lowercase letter " << c;
        EXPECT_TRUE(IsValidQKeyCode(events[0].code));
    }
}

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android

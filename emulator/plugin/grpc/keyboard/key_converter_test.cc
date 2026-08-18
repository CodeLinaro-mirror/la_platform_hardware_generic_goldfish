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

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android

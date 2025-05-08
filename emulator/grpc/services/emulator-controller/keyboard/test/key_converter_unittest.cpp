#include <gtest/gtest.h>

#include <vector>

#include "aemu/base/ArraySize.h"
#include "android/emulation/control/keyboard/key_conversion.h"
#include "dom_key.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/input.h"

// IWYU pragma: end_keep
// clang-format on
}

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

extern "C" QKbdState* qkbd_state_init(QemuConsole* con) {
    return nullptr;
}
extern "C" void qkbd_state_key_event(QKbdState* kbd, int qcode, bool down) {};
extern "C" int qemu_input_linux_to_qcode(unsigned int lnx) {
    return 0;
}

// Function to check if a QKeyCode is valid
bool isValidQKeyCode(QKeyCode code) {
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
            EXPECT_TRUE(isValidQKeyCode(qcode))
                    << "Invalid QKeyCode " << (int)qcode << " for usb code 0x" << std::hex
                    << entry.usb << std::dec << " for evdev code 0x" << std::hex << entry.evdev
                    << std::dec << " (" << entry.code << ") at index " << idx;
        }
    }
}

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android

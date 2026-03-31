#include "qemu/osdep.h"
#include "ui/input.h"
#include "ui/kbd-state.h"

QKbdState* qkbd_state_init(QemuConsole* con) {
    return NULL;
}

void qkbd_state_key_event(QKbdState *kbd, QKeyCode qcode, bool down) {}

int qemu_input_linux_to_qcode(unsigned int lnx) {
    return 0;
}

const guint qemu_input_map_linux_to_qcode_len = 0;
const guint16 qemu_input_map_linux_to_qcode[] = {};

const guint qemu_input_map_qcode_to_linux_len = 0;
const guint16 qemu_input_map_qcode_to_linux[] = {};

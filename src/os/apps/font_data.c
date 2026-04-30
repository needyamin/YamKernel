#include <stdint.h>

/*
 * Userspace apps use the same 8x16 ASCII bitmap as the kernel/compositor.
 * The older userspace table only defined a few glyphs, which made most app
 * text look blank. Keep this file as the userspace font translation unit so
 * app build rules do not need to change.
 */
#include "../../lib/font.c"

/* Compatibility symbol for older app code that still declares this table. */
const uint8_t font_basic_8x16[128][16] = {
    [32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

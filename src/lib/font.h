/* ============================================================================
 * YamKernel — Shared Bitmap Font
 * Embedded 8x16 bitmap font (CP437 subset: ASCII 32-126)
 * ============================================================================ */
#ifndef _LIB_FONT_H
#define _LIB_FONT_H

#include <nexus/types.h>

#define FONT_W 8
#define FONT_H 16

/* External reference to the 95x16 font array */
extern const u8 yam_font_data[95][16];

#endif /* _LIB_FONT_H */

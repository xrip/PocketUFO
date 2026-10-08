/* pufo_common.h - shared base types for the PocketUFO Win64 recreation.
 *
 * The original is WinCE/ARM32 (pufobeta.exe.i64). Types here mirror the IDB
 * ones so recreated code reads like the decompilation. See docs/idb/MAP.md.
 */
#ifndef PUFO_COMMON_H
#define PUFO_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The original is 32-bit ILP32. Fixed-width types are used everywhere in the
 * port so struct layouts and file formats stay byte-exact. */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;

/* Display modes resolved at startup. Verified against InitInstance @ 0x9fd48. */
typedef enum DisplayMode {
    DISP_240x320 = 0, /* native, no scaling */
    DISP_320x240 = 1,
    DISP_480x640 = 2,
    DISP_640x480 = 3
} DisplayMode;

/* Software surface: the original renders into a 240x320 16bpp buffer and
 * blits it to the GAPI framebuffer. On Win64 the framebuffer is a DIB section.
 */
#define PUFO_SURF_W 240
#define PUFO_SURF_H 320
#define PUFO_SURF_BPP 2 /* RGB565 */

#endif /* PUFO_COMMON_H */

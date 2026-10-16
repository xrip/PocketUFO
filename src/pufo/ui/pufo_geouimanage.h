/* pufo_geouimanage.h - the geoscape UI's owned objects and its teardown.
 *
 * Evidence: GeoscapeUi_FreeAll @ 0x86d20.
 *
 * This is the other half of the geoscape UI lifecycle: Geoscape_InitUI (0x857f4) builds
 * these objects and GeoscapeUi_FreeAll @ 0x86d20 releases them. The port keeps the whole
 * owned set in one struct so the teardown is a function over data instead of a walk over
 * forty globals.
 *
 * What the original frees, in the order it frees it:
 *
 *   1. KillTimers_D, clear Map.bGeoscapeUi, Font_Free(fontSys @ 0x158268).
 *   2. Thirteen widget pointers held at 0x1864F8 + 4*i, i = 0..12. 0x1864F8 is the
 *      geoscape UI root: the same block Geoscape_CloseUI is called with, the same block
 *      whose +0xC8 (0x1865C8) is the 24-byte mission-site table, and the same block whose
 *      +0x3C (0x186534) is the strateg PtrArray. ARM at 0x87204 proves the last one:
 *      `ADD R0, R9, #0x3C` with R9 = 0x1864F8.
 *   3. Thirteen pad singletons, then the world-size block, then one more pad. The
 *      world-size entry is different from the rest: 0x186BC0 does not hold the pair, it
 *      holds a POINTER to it (Smap_ZoomOut at 0x81D90 does `LDR R1,[R1] / STRH R3,[R1,#2]`),
 *      and it is deleted with no null check and no PadImage_FreeData.
 *   4. Ten pad pointers at 0x186BF0 + 4*i, i = 0..9.
 *   5. Thirteen widget singletons.
 *   6. Only when the argument is non-zero, the game world itself: soldiers, bases, craft,
 *      and the strateg PtrArray.
 *   7. The status line panel, then a tail call to Sound_Shutdown, whose value is returned.
 *
 * The three world counts are read at three different widths, and the ARM is explicit:
 *
 *     soldiers  LDRH  [Map + 0x1998]  -> 0x115350, unsigned 16-bit, gate is `<= 0`
 *     bases     LDRSB [0x159768+0x618] -> 0x159D80, SIGNED byte, gate is `<= 0`
 *     craft     LDRB  [Map + 0x1194]  -> 0x114B4C, unsigned byte, gate is `<= 0`
 *
 * 0x159D80 is the same byte the save layer writes as the bases chunk's base count and the
 * same byte BaseRec_ctor indexes auPerBaseVal with. Hex-Rays shows it as
 * `gUiRootPanel.anLineAux[79]`, which is the same address: 0x158890 + 0x13B4 + 4*79.
 *
 * The arrays sit at Map + 0x1174 (bases), Map + 0x1198 (soldiers) and Map + 0x10AC (craft),
 * read out of the ARM's folded immediates at 0x8713C, 0x87190 and 0x871DC.
 *
 * unk_186B08 is not a game variable. It is the compiler's spilled loop counter: every loop
 * in this function stores its counter through that one global. Nothing reads it afterwards.
 */
#ifndef PUFO_GEOUIMANAGE_H
#define PUFO_GEOUIMANAGE_H

#include "audio/pufo_audio.h"
#include "game/pufo_font.h"
#include "game/pufo_mapctx.h"
#include "game/pufo_ship.h"
#include "game/pufo_strateg.h"
#include "game/pufo_timer.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"
#include "ui/pufo_panel.h"
#include "util/pufo_ptrarray.h"

#define PUFO_GEOUI_ROOT_CTLS 13
#define PUFO_GEOUI_PAD_SLOTS 13
#define PUFO_GEOUI_CLOCK_DIGIT_PADS 10

/* The thirteen widget singletons, in the order the original frees them (0x86F70..0x87104). */
typedef enum GeoscapeUiCtlSlot {
    GEOUI_CTL_186B00 = 0,
    GEOUI_CTL_186BA4,
    GEOUI_CTL_186BB4,
    GEOUI_CTL_186BB0,
    GEOUI_CTL_186BAC,
    GEOUI_CTL_186BA8,
    GEOUI_CTL_186B9C,
    GEOUI_CTL_186B98,
    GEOUI_CTL_186B94,
    GEOUI_CTL_186B90,
    GEOUI_CTL_186B8C,
    GEOUI_CTL_18704C,
    GEOUI_CTL_187024
} GeoscapeUiCtlSlot;

/* The thirteen pad singletons, in the order the original frees them (0x86D88..0x86F28).
 * The world-size block sits between slot 1 and slot 2 in the original; the port frees it
 * after the array, which is not observable in a program that does not double-free. */
typedef enum GeoscapeUiPadSlot {
    GEOUI_PAD_186BA0 = 0,
    GEOUI_PAD_186C18,
    GEOUI_PAD_186C28,   /* zoom level 0 overlay */
    GEOUI_PAD_186C1C,   /* zoom level 1 overlay */
    GEOUI_PAD_186BB8,   /* zoom level 2 overlay */
    GEOUI_PAD_186B78,   /* zoom level 0 size source */
    GEOUI_PAD_186B74,   /* zoom level 0 tiles */
    GEOUI_PAD_1864F0,   /* zoom level 1 size source */
    GEOUI_PAD_187064,   /* zoom level 1 tiles */
    GEOUI_PAD_1864EC,   /* zoom level 2 size source */
    GEOUI_PAD_18705C,   /* zoom level 2 tiles */
    GEOUI_PAD_186BBC,
    GEOUI_PAD_187048
} GeoscapeUiPadSlot;

typedef struct GeoscapeUiState {
    /* 0x1864F8: the geoscape UI root. Its first thirteen dwords are these widgets. */
    UiControl *apRootCtl[PUFO_GEOUI_ROOT_CTLS];
    UiControl *apCtl[PUFO_GEOUI_PAD_SLOTS];
    PadImage  *apPad[PUFO_GEOUI_PAD_SLOTS];
    PadImage  *apClockDigitPads[PUFO_GEOUI_CLOCK_DIGIT_PADS]; /* 0x186BF0 + 4*i */

    /* 0x186BC0 holds a pointer to the pair, not the pair itself. Freed with no null
     * check and no PadImage_FreeData, because it is not a PadImage. */
    SmapWorldSize *pWorldSize;

    UiPanel *pStatusPanel;   /* unk_18641C */
    PufoFont *pFont;         /* fontSys @ 0x158268 */
    u8 *pbGeoscapeUi;        /* Map.bGeoscapeUi @ +0x44 */
} GeoscapeUiState;

/* The world GeoscapeUi_FreeAll can release. Counts carry the original's widths because the
 * gates differ: the soldier count is 16-bit, the base count is a SIGNED byte, the craft
 * count is an unsigned byte. */
typedef struct GeoscapeRoster {
    Soldier **apSoldiers;    /* Map + 0x1198 */
    u16       nSoldierCount; /* Map + 0x1998 */
    BaseRec **apBases;       /* Map + 0x1174 */
    i8        nBaseCount;    /* 0x159D80 */
    Ship **apCraft;          /* Map + 0x10AC */
    u8        nCraftCount;   /* Map + 0x1194 */
    PtrArray *pStrategList;  /* 0x1864F8 + 0x3C = 0x186534 */
} GeoscapeRoster;

/* GeoscapeUi_FreeAll @ 0x86d20. bFreeWorld is the original's `char a1`: zero releases only
 * the UI, non-zero also releases the soldiers, bases, craft and the strateg list. The
 * bankruptcy path in Geoscape_CloseUI case 19 calls it with 1.
 *
 * Two places the port refuses what the original does:
 *   - The original writes `pStatusPanel->nLineCount = 20` at panel + 0xBDC BEFORE testing
 *     pStatusPanel for null (0x87220 then 0x8722C). The port tests first.
 *   - The original deletes *(u32*)0x186BC0 with no null check at all. The port skips a null
 *     world-size block.
 *   - The original ends in `B Sound_Shutdown` and hands back whatever that leaves in R0.
 *     The port's Sound_Shutdown is void, so there is no value to hand back and this function
 *     is void. Nothing in the mapped call sites reads it. */
void GeoscapeUi_FreeAll(GeoscapeUiState *pUi, const GeoscapeRoster *pRoster, char bFreeWorld);

#endif /* PUFO_GEOUIMANAGE_H */

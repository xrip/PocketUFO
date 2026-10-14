/* pufo_geoiinit.h - Geoscape_InitUI @ 0x857f4: the geoscape bring-up.
 *
 * Evidence: Geoscape_InitUI @ 0x857f4 (5420 bytes), fully decompiled with the ARM
 * checked at every store the decompiler rendered through a guessed type.
 *
 * This is the other half of the geoscape UI lifecycle: it builds exactly the objects
 * GeoscapeUi_FreeAll @ 0x86d20 releases (see pufo_geouimanage.h), and it is reached
 * from three places: the start menu's new-game button (MenuCtl0_OnClick @ 0x1a09c with
 * mode 0), LoadGame @ 0x44bbc's teardown paths (mode 1), and the escape path
 * Tact_Shutdown(0) + Geoscape_InitUI(1).
 *
 * What it does, in the original's own order:
 *
 *   1. Sets the nine save-state bytes at 0x18655D to 1 (abUnk18655D in pufo_save.h),
 *      runs Font_Init, clears the screen id (nGeoscapeScreen @ 0x186BC4), sets the
 *      geoscape flag byte (unk_1139FC @ Map+0x44, a STRB), and clears the two root
 *      flags at 0x18652C / 0x18652D (byte gates read by BaseFacScreen_DrawPlacementOverlay
 *      @ 0x58E7C and BasePlaneScreen_Draw @ 0x7D3D0).
 *   2. Creates the thirteen pad singletons (GeoscapeUiState.apPad), the world-size block
 *      (0x186BC0: a pointer to a u16 pair, 800 x 400), and the ten clock digit pads
 *      "b0".."b9" (apClockDigitPads @ 0x186BF0).
 *   3. Fills the seventeen facility records at aBaseFacFlags @ 0x186E48: name from a text
 *      record, bFlag21 = the record index, and the initial cost. The costs are the
 *      literal pool values; facility 10's cost renders in IDA as `LDR R2, =unk_13D620`
 *      but the literal is the number 0x13D620 = 1300000 - BaseFacScreen_TryBuySelected
 *      @ 0x58910 reads the cost field as a plain number in its afford test.
 *   4. Loads the four geoscape sprite groups through Font_LoadSpr, capturing each group's
 *      first frame index before the load: font -> fontSys_nGlyphBase (+0x28), basebits ->
 *      +0x2C, mapbits -> +0x34, bigobs -> nBigObsSprBase (+0x26), then Font_BuildIndex.
 *   5. Loads 23 palettes, then the 24th (earth100.pal) later with the earth pads.
 *   6. Creates the thirteen control singletons (apCtl) and positions them with the
 *      descriptors at 0xAC078 (g_aGeodescSingle), in the original's own singleton order:
 *      slot i gets descriptor g_anGeodescSingleSlot[i].
 *   7. Creates the thirteen root controls (apRootCtl), stores the click handlers, then
 *      positions them with the descriptors at 0xABED8 (g_aGeodescRoot). The handler slots
 *      are the root block's own dwords: [0] Smap_OnZoomOut (0x1864F8), [1] Smap_OnZoomIn
 *      (0x1864FC), [2..7] the six speed handlers (apGeoscapeSpeedCtl @ 0x186500),
 *      [8] Ui_OpenScreen18 (0x186518), [9] Ui_CloseScreen18 (0x18651C),
 *      [10] Base_EnterBuildMode (0x186520), [11] Ui_OpenBaseInfo (0x186524),
 *      [12] Ui_OpenFunding (0x186528). The 186B8C singleton gets
 *      Smap_BeginCraftSelectBtn (0x865D0). The port takes all fourteen as function
 *      pointers because UiControl.pfnClick is a no-argument pointer and the handlers
 *      need environments.
 *   8. Publishes the three zoom layer sets (SmapZoomSet) and sets the viewport to level 0
 *      with the level-0 grid's size (0x1863F4 / 0x1863F8 are gSmapCam.nMapW / nMapH).
 *   9. Only when the low byte of nMode is zero: reads research.dat into the live topic
 *      array, zeroes the u16 at +6 of every topic (ARM `STRH R4,[R1,#6]` - Hex-Rays
 *      renders this as wProgress through its save-order ResearchTopic type; in the
 *      in-memory order it is ResearchTopicLive.wUnk6), clears the viewport scroll and
 *      drag deltas, and runs Game_StartNew (hooked - not ported yet).
 *  10. GameTime_Decode; per base Base_ClearFacilityGridRow into the site-state array;
 *      the initial object prices from dword_AA2C0 into ObjectRecFull.uPrice (slots
 *      1..nObjectCount-1); object names from text records 1447..1594 into slots 0..147
 *      (148 copies, not nObjectCount); apObjects[1].uFirePower = 110 (`STRH R3,[R0,#0xB6]`);
 *      unk_159DC4 = 10; GameTime_Set5Sec; StartTimers_D.
 *  11. Builds the status line panel (unk_18641C): UiPanel_ctor(20), nStyle 13, nFont 16,
 *      the three SetBytes groups, nUnk7076 1, bFlag7073 1, pfnClick Geoscape_OnRadarListClick
 *      (hooked), bUnk7088 100.
 *  12. Reads the 56 mission-site records: 24 bytes from "<i>.cts" into unk_1865C0, then the
 *      name from text record 1655+i into +4. With lang.dat present only 1650 text records
 *      are loaded (Assets_LoadStartup @ 0x384e0 takes the lang copy), so those 56 records
 *      are zero in the binary and the names come out empty - the original's own behaviour.
 *  13. Clears the clock pause byte (0x15961C = apLines[106] = GameTime.bPaused), sets the
 *      save format version (nSaveFmtVer @ 0x18655C) to 3, and runs the sound bring-up:
 *      Sound_InitAndLoadSamples("tact"), Sound_LoadAndPlayMusic(2), and the two volume
 *      applies from the options.
 *
 * Deviations from the original, all deliberate:
 *   - The world-size block: the original allocates 0x4E204 bytes (800*400 + the u16 pair)
 *     and memsets it; the port allocates just the SmapWorldSize pair. Nothing in the
 *     mapped code reads the 320000-byte tail - the tile grid lives in the m100 pad.
 *   - The name copies are bounded to the destination field (the original copies until NUL
 *     with no bound; the shipped names fit).
 *   - research.dat is read capped at the topic array size (the shipped file is exactly
 *     144 * 34 = 4896 bytes).
 *   - The original's `dword_186B08` loop counters are the compiler's spilled locals (see
 *     pufo_geouimanage.h); the port uses real locals.
 *   - The original stores the research entry count with STRB (a byte); the port writes the
 *     u16 the port family carries at 0x160C4C, which is the same value at startup.
 */
#ifndef PUFO_GEOIINIT_H
#define PUFO_GEOIINIT_H

#include "asset/pufo_asset.h"
#include "audio/pufo_audio.h"
#include "game/pufo_baseui.h"   /* ResearchTopicLive */
#include "game/pufo_clock.h"
#include "game/pufo_font.h"
#include "game/pufo_geoscape.h" /* GeoscapeWorld, GameTime_Set5Sec, Base_ClearFacilityGridRow */
#include "game/pufo_objectrec.h"
#include "game/pufo_options.h"
#include "game/pufo_strateg.h"  /* SmapView, SmapZoomSet, SmapWorldSize */
#include "pufo_common.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geouimanage.h"
#include "ui/pufo_panel.h"

/* The 28-byte facility record at aBaseFacFlags @ 0x186E48. Save_WriteBases @ 0x3F2CC saves
 * one byte per record (bFlag20), which is why the save docs call this table BaseFacFlag. */
#define PUFO_BASEFAC_COUNT 17
typedef struct BaseFacFlag {
    char szName[20]; /* +0, from a text record */
    u8   bFlag20;    /* +20, the saved place flag; Game_StartNew sets it, InitUI leaves 0 */
    u8   bFlag21;    /* +21, InitUI stores the record index here */
    u8   gap22[2];   /* +22 */
    u32  uCost;      /* +24, read as a number by BaseFacScreen_TryBuySelected @ 0x58910 */
} BaseFacFlag;
_Static_assert(sizeof(BaseFacFlag) == 28, "aBaseFacFlags records are 28 bytes");

/* The 24-byte mission-site record at unk_1865C0 (the root block's +0xC8). The u32 comes
 * from the "<i>.cts" file; the name is copied from text record 1655+i over +4. */
#define PUFO_MISSION_SITE_COUNT 56
typedef struct MissionSiteRec {
    u32  uField0;    /* +0, from the .cts file */
    char szName[20]; /* +4 */
} MissionSiteRec;
_Static_assert(sizeof(MissionSiteRec) == 24, "the mission-site record is 24 bytes");

/* Everything Geoscape_InitUI reaches outside its own allocations. */
typedef struct GeoscapeInitEnv {
    /* The UI state InitUI fills (the same struct GeoscapeUi_FreeAll releases). */
    GeoscapeUiState *pUi;
    SmapView        *pView;      /* gSmapCam @ 0x1863E8 */
    SmapZoomSet     *pSetsOut;   /* three sets: the layer globals the zoom code swaps */
    /* The world-size pair (0x186BC0) is allocated here and published as pUi->pWorldSize;
     * GeoscapeUi_FreeAll releases it. */
    i32             *pnScreenMode;   /* nGeoscapeScreen @ 0x186BC4 */
    u8              *pbGeoscapeUi;   /* unk_1139FC @ Map+0x44 */
    u8              *pbRoot34;       /* unk_18652C */
    u8              *pbRoot35;       /* unk_18652D */

    PufoFont *pFont;             /* fontSys @ 0x158268 */

    AssetReader  *pRdr;          /* pRdr @ 0x187028 */
    const u8     *pTextRecs;     /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */

    GameTime         *pTime;     /* nGameTimeSec @ 0x159618; bPaused is 0x15961C */
    ResearchTopicLive *apTopics; /* the live topic array @ 0x15EF0C */
    u16              *puResearchEntries; /* 0x160C4C, the shared entry count */

    BaseFacFlag *paFacFlags;     /* aBaseFacFlags @ 0x186E48 */

    ObjectRecFull *const *apObjects; /* Map.apObjects @ 0x113AC0 */
    i32           nObjectCount;      /* Map_nObjectCount @ 0x114A60 */
    i32           *pnGlyphPalBase;   /* Map_nGlyphPalBase @ 0x1183F4 */

    u8            *abFacilityGrid;   /* gBaseFacGrid @ 0x159768 */
    GeoscapeWorld *pWorld;           /* apSiteState is the byte_161F38 array */
    i8            *pbBaseCount;      /* nBaseCount @ 0x159D80, signed */
    i32           *pnUnk159DC4;      /* 10 */

    MissionSiteRec *paMissionSites;  /* unk_1865C0, 56 records */

    u8 *abUnk18655D;   /* nine bytes @ 0x18655D, set to 1 */
    i8 *pbSaveVersion; /* nSaveFmtVer @ 0x18655C, set to 3 */

    const PufoOptions *pOptions; /* the two volume applies read these */

    /* The fourteen click handlers, in the root block's own slot order (see the header
     * comment). UiControl.pfnClick takes no arguments, so the app wires these as thunks
     * over its own environments. */
    void (*apfnRootClick[PUFO_GEOUI_ROOT_CTLS])(void);
    void (*pfnCraftSelectClick)(void); /* Smap_BeginCraftSelectBtn @ 0x84634 -> 186B8C */
    void (*pfnRadarClick)(void);       /* Geoscape_OnRadarListClick @ 0x854c4, not ported */

    /* Game_StartNew @ 0x94554 is not ported yet; the mode-0 branch calls this hook. */
    void (*pfnStartNew)(void *pUser);
    void *pHookUser;

    /* UiControl_Setup's pad loader injection (it reads the asset archives). */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pLoadPadsUser;
} GeoscapeInitEnv;

/* Geoscape_InitUI @ 0x857f4. nMode is tested as its low byte: zero runs the new-game
 * branch (research.dat load, viewport clear, Game_StartNew); any other value skips it
 * and leaves the loaded world alone. */
void Geoscape_InitUI(const GeoscapeInitEnv *pEnv, i32 nMode);

#endif /* PUFO_GEOIINIT_H */

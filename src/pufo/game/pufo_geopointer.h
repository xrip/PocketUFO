/* pufo_geopointer.h - the geoscape pointer dispatcher, both halves:
 * Geoscape_OnPointer @ 0x872d0 (8,576 B) and Geoscape_OnPointerUp @ 0x89450 (6,992 B).
 *
 * Evidence: both functions decompiled in full and checked against the ARM at every branch
 * the decompiler rendered through a guessed type: the entry stores (0x872F8 / 0x8731C are
 * STRH, so the cursor pair is 16-bit), the tap division (0x87E94..0x87EAC), the drag gate
 * (0x87C48..0x87CE4), the craft-record byte reads (0x8851C / 0x88598), the signed base-count
 * product (0x88AC4 and 0x8AC8C, both LDRSB at abFacilityGrid + 0x618), the options control
 * arrays (0x89170 / 0x892E8), the stat-panel gates (0x88674..0x88748 and 0x8A370..0x8A460),
 * the release arm's control-slot immediates (0x89F70 is [base,#0x148], 0x8A09C is [base,#0x150])
 * and the shortfall gate (0x89584 reads 0x1870C8 as a DWORD).
 *
 * The press's only caller is Input_OnPointer @ 0x13c6c at 0x13CE4 and the release's is
 * Input_OnPointerUp @ 0x148d0 at 0x1490C; both pass a1 = 0x1864F8 (the geoscape UI root,
 * IDA's GeoscapeScreenBlock), and the press also gets x, y and the pen state. The port reaches
 * them through the TactInputHooks seam pfnGeoscapePointer / pfnGeoscapePointerUp. Neither
 * caller reads the return value - 0x14910 branches straight away.
 *
 * ## The shape
 *
 * It is a dispatcher over the geoscape screen id (nGeoscapeScreen @ 0x186BC4), and each
 * arm hit-tests the controls that screen owns. Nothing here decides game rules: it hands
 * the pointer to widgets, and the two arms that do change world state are the globe drag
 * and the base-tab strip.
 *
 *   1. Entry (0x872F0..0x87324): store the cursor pair as two HALFWORDS, clear the
 *      placement-fail byte at 0x1870B8, and read the screen id.
 *   2. Screen 0 only (0x87324..0x87424): if the root message list's line-0 flag is in
 *      (30, 50) the main-menu controls own the pointer - flag 37 adds the two dialog
 *      controls - and the function returns without touching anything else.
 *   3. The switch (0x8744C) covers ids 0..12, 15..20. Ids 13, 14 and anything else fall
 *      to the default, which returns the cursor x it was handed.
 *   4. Ids 0 and 2 share the world arm (LABEL_13 @ 0x87530): a chain of message-flag
 *      tests, then the drag / strateg-select block, then - only when no drag is running
 *      and the screen is not 2 - the thirteen root controls.
 *
 * ## The world arm's drag block (0x87C2C..0x87D14)
 *
 * byte_187074[0] @ 0x187074 is the drag flag. It is cleared when a press lands on a
 * selectable strateg object, and set when a drag starts. The gate that starts one is
 *
 *     bDown && cursorY < 320 - [0x186BBC]->uHeight && (cooldown >= 6 || no select hit)
 *
 * - with a cooldown below 6 a press on a selectable object resolves the tile under the
 *   pointer (Smap_GetTileAtScreen), sets the cooldown to 255 and returns 255;
 * - otherwise the drag sets the flag, zeroes the cooldown and records the cursor into
 *   SmapView.nLastDx / nLastDy.
 *
 * Smap_ScrollBy then runs only while the drag flag is set AND the root message list is
 * empty (0x87CF8: gUiRootPanel_nLineCount == 0) - a message on the status line stops the
 * globe from scrolling under the pointer.
 *
 * ## The release half (Geoscape_OnPointerUp @ 0x89450)
 *
 * It is not the press with bDown flipped. It takes only the block - every coordinate it uses is
 * the cursor pair the press stored, read back with LDRSH - and every UiControl_HitTest it makes
 * is a release (x = y = 0, bDown = 0, bForceClick = 0; a release ignores the hit box).
 *
 *   1. Screen 0's menu band again (0x8946C..0x89568), but the sample plays first and the flag-37
 *      pair is released BEFORE the two menu controls, where the press released them after.
 *   2. The shortfall gate (0x89578..0x89600): when the dword at 0x1870C8 is exactly 1 and the
 *      screen is not 0, the report is cleared here unless the screen is 6, 9, 10, 11 or 12 - the
 *      screens that draw the shortfall panel. Screen 9 also drops both popup bytes when no product
 *      is selected, and plays the sample when one is.
 *   3. The world arm (LABEL_25 @ 0x896B4) clears the drag flag (0x896BC) instead of running the
 *      drag block, plays Sound_Play(17) in almost every flag arm, and merges flags 17 with 15
 *      (LABEL_30 @ 0x89760), flags 21/22/23 with 12 (LABEL_52 @ 0x89AA0), flags 10 with 60 and
 *      flags 13 with 27. With no flag matched it clears the target-select flag (0x89CD8), returns
 *      2 on screen 2, and otherwise releases all thirteen root controls.
 *   4. Where the press only tracked the hovered row, the release clicks the list:
 *      UiPanel_ClickRow @ 0x3eee0 on the soldier-name, research, manuf, buy/sell, plane, screen-16
 *      and psi panels. The press arm's pPlaneCraftStatusPanel and pPlaneSlotPanel clicks have no
 *      release counterpart at all.
 *   5. One tail is shared by two arms (0x89B64..0x89B90): click the row under the cursor, then set
 *      the panel's bFlag7073. The world arm's multi-select gate reaches it with the status panel
 *      (0x18641C) and the options confirm tail jumps to it with pOptPanelShips (0x8AF1C).
 *
 * ## Deviations from the original
 *
 *   1. A NULL control slot is skipped. The original dereferences every slot it indexes except the
 *      ten base-strip slots (0x87E38 in the press, 0x89DA4 in the release do check those), so a
 *      screen that was never built would fault there; the port refuses instead. Recorded in
 *      docs/parity/bugs.tsv.
 *   2. Smap_HitTestStrategSelect (0x82be8, 3,088 B) is not ported, so the two strateg select
 *      tests are injected through pfnHitTestStrategSelect. The release half never calls it.
 *   3. The original re-reads the line-0 flag before every test in the world arm. Nothing writes
 *      the list in between, so the port reads it once.
 *   4. The original's loop counters are the spilled global dword_186B08 (see
 *     pufo_geouimanage.h); the port uses real locals.
 *   5. Three release arms hand back a value that is an address rather than a result: 0x895C8
 *      returns the address of byte_1870C8, and the shared panel tail at 0x89B90 returns the
 *      UiPanel pointer it just clicked (reached from the multi-select gate and from the options
 *      confirm tail). The port returns 0 there. No caller reads it - Input_OnPointerUp branches
 *      away at 0x14910 - so this is invisible in the original too.
 */
#ifndef PUFO_GEOPOINTER_H
#define PUFO_GEOPOINTER_H

#include "game/pufo_geoscape.h"  /* BaseStripHitEnv, Base_HitTestFacilityStrip, PUFO_RADAR_CONTROLS */
#include "game/pufo_ship.h"      /* Ship */
#include "game/pufo_strateg.h"   /* SmapView, Smap_ScrollBy, Smap_GetTileAtScreen */
#include "pufo_common.h"
#include "render/pufo_render.h"  /* PadImage */
#include "ui/pufo_control.h"
#include "ui/pufo_geouimanage.h" /* GeoscapeUiCtlSlot, PUFO_GEOUI_ROOT_CTLS */
#include "ui/pufo_linelist.h"
#include "ui/pufo_panel.h"

/* The main-menu band of the root message list's line-0 flag (0x87334 / 0x87340), and the
 * value inside it that adds the two dialog controls (0x87384). */
#define PUFO_GEO_MENU_FLAG_MIN 30
#define PUFO_GEO_MENU_FLAG_MAX 50
#define PUFO_GEO_MENU_FLAG_DIALOG 37

/* The control-slot block the original keeps after the facility grid. The strip is
 * gBaseFacGrid + 0x120 = 0x159888 (apNavCtl, case 1 at 0x87E34), the two soldier-list page
 * buttons follow it at +0x148 = 0x1598B0 (apSoldierPanelCtl, case 4 at 0x8809C / 0x89F68) and
 * the three soldier-find buttons at +0x150 = 0x1598B8 (apSoldierFindCtl, case 5 at 0x8A094). */
#define PUFO_GEO_STRIP_CTLS 10

/* The plane screen's control slots, laid out after pPlaneBackdropPad @ 0x161A48. The
 * indices below are the array positions the original's folded immediates address:
 * +0x30 is index 0, so index i is pPlaneBackdropPad + 0x30 + 4*i. */
#define PUFO_GEO_PLANE_CTLS 15
#define PUFO_GEO_PLANE_WEAPON_CTL  0  /* +0x30, one per Ship.bCrewSlots */
#define PUFO_GEO_PLANE_DOGFIGHT_CTL 2 /* +0x38, +0x3C */
#define PUFO_GEO_PLANE_STAT_CTL_A  4  /* +0x40, unk_161A88 */
#define PUFO_GEO_PLANE_STAT_CTL    5  /* +0x44..+0x58, unk_161A8C..dword_161A9C */
#define PUFO_GEO_PLANE_CREW_CTL    11 /* +0x5C, +0x60 */
#define PUFO_GEO_PLANE_STORE_CTL   13 /* +0x64, +0x68 */
#define PUFO_GEO_PLANE_STAT_COUNT  6

/* The options screen's control groups (0x892E8, 0x89170, 0x89370):
 * aOptionsListCtl @ 0x159E20 holds seven, pOptCtlPrev / pOptCtlNext follow at
 * 0x159E3C / 0x159E40, and the page-4 loop runs over aOptionsMusicBtn @ 0x159E44 and
 * aOptionsSfxBtn @ 0x159E5C - twelve slots, which is what the ARM's
 * `ADD R0, R11, R0, LSL#2 / LDR R0, [R0, #0x24]` reaches. */
#define PUFO_GEO_OPT_LIST_CTLS   7
#define PUFO_GEO_OPT_SOUND_CTLS  6

/* Screen 16's control slots at unk_1872E0 + 8 (0x88E30). */
#define PUFO_GEO_SCREEN16_CTLS 6

/* Everything Geoscape_OnPointer reaches. Every pointer is the original's own global or
 * the object its global points at; the address is in the comment. */
typedef struct GeoscapePointerEnv {
    /* The 0x1864F8 geoscape UI root (GeoscapeScreenBlock): the thirteen control slots
     * and the two popup bytes at +0x34 / +0x35. */
    UiControl *const *apRootCtl;  /* apObj @ 0x1864F8 + 4*i */
    u8  *pbPopupA;                /* bPopupVisibleA @ 0x18652C (unk_18652C) */
    u8  *pbPopupB;                /* bPopupVisibleB @ 0x18652D (unk_18652D) */

    /* The thirteen geoscape control singletons, indexed by GeoscapeUiCtlSlot. Each is
     * the object the original's global slot points at, so the env carries the object. */
    UiControl *const *apCtl;

    /* Screen state. */
    const i32 *pnScreenId;        /* nGeoscapeScreen @ 0x186BC4 */
    UiLineList *pLines;           /* the root message list @ 0x158890 */

    /* Cursor, tap and drag. The cursor pair is 16-bit in the original (STRH / LDRSH). */
    i32 *pnCursorX;               /* nSmapCursorX @ 0x186BD0 */
    i32 *pnCursorY;               /* nSmapCursorY @ 0x186BD4 */
    i32 *pnTapX;                  /* nSmapTapX @ 0x186B7C */
    i32 *pnTapY;                  /* nSmapTapY @ 0x186B80 */
    u8  *pbDragActive;            /* byte_187074[0] @ 0x187074 */
    u8  *pbNewBaseMode;           /* bBaseFacDone @ 0x187078, also byte_187074[4] */
    u8  *pbPlaceFail;             /* bBasePlaceFail @ 0x1870B8 */
    u8  *pbPlaceActive;           /* bBasePlaceActive @ 0x1870BC */
    u8  *pbTargetSelectActive;    /* unk_187068 */
    u8  *pbInterceptDone;         /* byte_18709C[0] @ 0x18709C */
    u8  *pbMultiSelectPanel;      /* byte_18709C[4] @ 0x1870A0 (bSmapMultiSelectPanel) */
    const u8 *pbPlacementPending; /* bBaseFacConfirm @ 0x1870AC */
    const u8 *pbPendingRegionId;  /* bPendingRegionId @ 0x159DF4; 0xFF means none pending */
    const u8 *pbMapSelectionFlag; /* bMapSelectionFlag @ 0x159E84 */

    /* The globe. */
    SmapView *pView;              /* gSmapCam @ 0x1863E8 */
    const PadImage *pZoomOverlay; /* the object [0x186BBC] points at; its uHeight is the strip */
    const u8 *pTiles;             /* the current zoom tile grid, [0x186B04]->pData */
    i32 *pnTapCooldown;           /* nRadarCooldownR @ 0xAC218 */
    i32 *pnRadarPulseTimer;       /* nRadarPulseTimer @ 0xAC224 */
    const u8 *pbRadarScreenActive;/* bRadarScreenActive @ 0x159678 */
    UiControl *const *apRadarCtl; /* aRadarControls @ 0x159684, PUFO_RADAR_CONTROLS */
    UiPanel *pStatusPanel;        /* unk_18641C */

    /* The base-tab strip (screens 1, 3 and 17). */
    BaseStripHitEnv stripHit;     /* the gate bytes Base_HitTestFacilityStrip tests */
    UiControl *const *apStripCtl; /* gBaseFacGrid + 0x120 @ 0x159888, PUFO_GEO_STRIP_CTLS */
    i8  *pnSelectedBaseIdx;       /* nSelectedBaseIdx @ 0x159D81 */
    const i8 *pnBaseCount;        /* 0x159D80, signed */
    u8  *pbTransferRowSel;        /* byte_1870F8 */
    const i8 *pnTransferBaseIdx;  /* byte_1870F4, read SIGNED (0x88AB0) */
    i8  *pbStripHitBase;          /* byte_158834 */
    const u8 *pbPsiPanelOpen;     /* byte_15EEF0 */
    u8  *pbPsiRowSel;             /* byte_15EE84, cleared by the screen-17 strip branch at 0x890C4 */
    UiPanel *const *apPsiPanel;   /* unk_15EED0, one per base */
    UiControl *const *apPsiCtl;   /* unk_15EE78 + 4 @ 0x15EE7C, two */
    UiControl *const *apBaseAtCtl;/* unk_158828 + 4 @ 0x15882C, two */

    /* The soldier screen (4, 5). */
    const u8 *pbSolScreenOpen;    /* 0x159DE0 */
    UiPanel *pSolPanelNames;      /* pSolPanelNames @ 0x15975C */
    UiPanel *pSolPanelEquip;      /* 0x159DF0 */
    UiControl *const *apSolPanelCtl; /* gBaseFacGrid + 0x148 @ 0x1598B0 (apSoldierPanelCtl), two */
    UiControl *const *apSolNameCtl;  /* gBaseFacGrid + 0x150 @ 0x1598B8 (apSoldierFindCtl), three */
    u8  *pbNameEditActive;        /* unk_186B88 */
    u8  *pbUiAnimStarted;         /* bUiAnimStarted @ 0x187080 */
    const u8 *pbTransferFlag;     /* bTransferFlag @ 0x159E10 */

    /* The build dialog (7). */
    UiPanel *pFacPanel;           /* pBaseFacPanel @ 0x15886C */
    const u8 *pbFacPlaceArmed;    /* bBaseFacPlaceArmed @ 0x158888 */
    UiControl *const *apFacCtl;   /* unk_158870 + 4 @ 0x158874, two */

    /* Research (8). */
    const u8 *pbRowSelected;      /* bUiRowSelected @ 0x160C80 */
    UiPanel *pResearchPanel;      /* pResearchTopicPanel @ 0x160C68 */
    UiControl *const *apResearchCtl; /* aResearchTopics + 0x1D48 @ 0x160C50, four */

    /* Manufacturing (9). */
    const u8 *pbProductSelected;  /* bManufProductSelected @ 0x15EE34 */
    UiPanel *pManufProductPanel;  /* pManufProductPanel @ 0x15EE44 */
    UiPanel *pManufPanelD;        /* pManufPanelD @ 0x15EE38 */
    UiControl *const *apManufCtl; /* unk_159E98 + 0x4F84 @ 0x15EE1C, six */

    /* The site-info screen (10) and buy/sell (11, 12). */
    UiControl *const *apSiteInfoCtl; /* 0x1870D8 + 4 @ 0x1870DC, four */
    UiControl *const *apBuySellCtl;  /* pBuySellScreenPad + 4 @ 0x161A0C, two */
    UiPanel *pBuySellPanelA;      /* pBuySellPanelA @ 0x161A1C */

    /* Screen 16 (the startup / load list). */
    const u8 *pb16ModeA;          /* byte_18730C[0] */
    const u8 *pb16ModeB;          /* unk_187310 */
    const u8 *pb16ListFull;       /* unk_1871FC */
    UiControl *const *ap16Ctl;    /* unk_1872E0 + 8 @ 0x1872E8, six */
    UiPanel *const *ap16PanelSet; /* dword_1871D0 */
    const i32 *pn16PanelIndex;    /* unk_1871F8 */
    UiPanel *p16Panel;            /* unk_187204 */

    /* Options (18). */
    const u8 *pbOptionsConfirm;   /* byte_159E80 */
    const u8 *pbOptionsPageActive;/* byte_159E88[4] @ 0x159E8C */
    const i8 *pnOptionsPage;      /* byte_159E88[0], read SIGNED (0x89128) */
    UiControl *const *apOptListCtl;  /* aOptionsListCtl @ 0x159E20, PUFO_GEO_OPT_LIST_CTLS */
    UiControl *const *apOptMusicCtl; /* aOptionsMusicBtn @ 0x159E44, PUFO_GEO_OPT_SOUND_CTLS */
    UiControl *const *apOptSfxCtl;   /* aOptionsSfxBtn @ 0x159E5C, PUFO_GEO_OPT_SOUND_CTLS */
    UiControl *pOptCtlPrev;       /* pOptCtlPrev @ 0x159E3C */
    UiControl *pOptCtlNext;       /* pOptCtlNext @ 0x159E40 */
    UiPanel *pOptPanelShips;      /* pOptPanelShips @ 0x159E18 */

    /* Screen 20. */
    const u8 *pbBaseAtDone;       /* bBaseAtDone @ 0x158854 */

    /* The craft screen (6). */
    Ship *const *apCraft;         /* Map + 0x10AC */
    const i32 *pnSelectedCraftIdx;/* nSelectedCraftIdx @ 0x161AC4 */
    UiControl *const *apPlaneCtl; /* pPlaneBackdropPad + 0x30, PUFO_GEO_PLANE_CTLS */
    const u8 *pbPlanePanelsBuilt;     /* bPlanePanelsBuilt @ 0x161F08 */
    const u8 *pbPlaneShowCraftDetail; /* bPlaneShowCraftDetail @ 0x161F20 */
    const u8 *pbPlaneStoresCommitted; /* bPlaneStoresCommitted @ 0x161F28 */
    const u8 *pbPlaneListBuilt;       /* bPlaneListPanelsBuilt @ 0x161F24 */
    const u8 *pbStatPanelAOpen;       /* bStatPanelAOpen @ 0x161F18 */
    const u8 *pbStatPanelBOpen;       /* bStatPanelBOpen @ 0x161F1C */
    UiPanel *pPlaneCraftPanel;        /* pPlaneCraftPanel @ 0x161EFC */
    UiPanel *pPlaneCraftStatusPanel;  /* pPlaneCraftStatusPanel @ 0x161ED0 */
    UiPanel *pPlaneSelRowPanel;       /* pPlaneSelRowPanel @ 0x161F00 */
    UiPanel *pPlaneCommitRowPanel;    /* pPlaneCommitRowPanel @ 0x161EEC */
    UiPanel *pPlaneSlotPanel;         /* pPlaneSlotPanel @ 0x161AC8 */
    UiPanel *pPlaneCrewPanel;         /* pPlaneCrewPanel @ 0x161EDC */
    /* The six stat-panel controls and the six count gates that release them, in the
     * order the original tests them (0x88674..0x88748): 0x161ED8, 0x161EE8, 0x161EF8,
     * 0x161EF0, 0x161ABC, 0x161ED4 against dword_161A8C..dword_161A9C. */
    const i32 *apanStatGate[PUFO_GEO_PLANE_STAT_COUNT];

    /* The soldier-name handlers installed into the two controls at 0x88244 / 0x88250.
     * UiControl.pfnClick takes no arguments, so the app wires these as thunks. */
    void (*pfnBackspaceName)(void);  /* Map_BackspaceSelectedName @ 0x63138 */
    void (*pfnCopyNameToEdit)(void); /* SoldierScreen_CopyNameToEdit @ 0x6318c */

    /* Injected callees. */
    void (*pfnAppendNameGlyph)(i32 nX, i32 nY, void *pUser); /* SoldierScreen_AppendNameGlyph 0x63208 */
    void (*pfnPlaySample)(i32 nSample, void *pUser);         /* Sound_Play @ 0x9e0c0, sample 17 */
    /* Smap_HitTestStrategSelect (0x82be8, 3,088 B) is not ported. */
    u8 (*pfnHitTestStrategSelect)(const SmapView *pView, i32 nX, i32 nY, void *pUser);

    /* Release only: the low-supply report the release gate reads and clears (0x89584 reads it as
     * a DWORD, 0x895C4 / 0x895D4 / 0x89600 clear it). */
    u32 *pdwShortfall;            /* byte_1870C8, read and written as a dword */

    /* Release only: the two buy/sell lists the site-info and buy/sell arms click (0x8A65C / 0x8A670,
     * 0x8A4EC / 0x8A500, 0x8A5AC / 0x8A5C0). The press arm tracks pBuySellPanelA instead. */
    UiPanel *pBuySellPanelB;      /* pBuySellPanelB @ 0x161A00 */
    UiPanel *pBuySellPanelC;      /* pBuySellPanelC @ 0x161A20 */

    /* Release only: the build dialog parks the tap here before it tests the hot corners. */
    i32 *pnPendingPlaceX;         /* nPendingPlaceX @ 0x15887C, from nSmapTapX (0x89E8C) */
    i32 *pnPendingPlaceY;         /* nPendingPlaceY @ 0x158880, from nSmapTapY (0x89E98) */

    /* What UiControl_HitTest needs outside the control (the pad blits and Map+0x60). */
    const UiControlHitCtx *pHit;
    void *pUser;
} GeoscapePointerEnv;

/* Geoscape_OnPointer @ 0x872d0. nX / nY are the pen position as Input_OnPointer hands
 * them over; bDown is the pen state (the original's a4, stored as a byte).
 *
 * The return value is whatever the taken arm leaves in R0 - a control's hit result, a
 * base index, the drag flag, or 255 from the strateg-select path. Input_OnPointer
 * discards it (0x13CE8 branches straight away), so no caller may read it as a status. */
i32 Geoscape_OnPointer(const GeoscapePointerEnv *pEnv, i32 nX, i32 nY, bool bDown);

/* Geoscape_OnPointerUp @ 0x89450. No coordinates: the original takes only the 0x1864F8 block and
 * reads the cursor pair Geoscape_OnPointer stored. Run it after the press, on the same env.
 *
 * The return value is whatever the taken arm leaves in R0 - a control's hit result, a row number,
 * the transfer row-selection flag, or (in three arms) an address; see deviation 5. Input_OnPointerUp
 * discards it (0x14910 branches straight away). */
i32 Geoscape_OnPointerUp(const GeoscapePointerEnv *pEnv);

#endif /* PUFO_GEOPOINTER_H */

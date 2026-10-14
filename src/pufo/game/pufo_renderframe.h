/* pufo_renderframe.h - the frame dispatcher.
 *
 * Evidence: Render_Frame @ 0x22310.
 *
 * The original reads everything through the Map block; the port passes one env that
 * carries the dispatch scalars and the per-branch sub-envs the callees already take.
 * The two callees the port does not have yet (Geoscape_Draw (0x8c180) and the present thunk
 * GX_Present (0x27788, a thunk to GX_PresentFrame @ 0x9ea7c, which the shell mirrors in
 * App_PresentLoadedSurface) are injected hooks.
 *
 * Dispatch, exactly as the ARM has it:
 *
 *   Map.bGeoscapeMode (unk_1139FC @ Map+0x44) != 0
 *                  -> return Geoscape_Draw(&screen block). NOTE: this branch returns
 *                     Geoscape_Draw's value and never reaches GX_Present -- Geoscape_Draw
 *                     presents itself (0x22328 / 0x22330).
 *   Map_nUiScreen == 0x10 -> Titles_DrawAnimatedFrame
 *   Map_nUiScreen == 0x11 -> MainMenu_Draw
 *   Map_nUiScreen == 0x12 -> Titles_DrawBackdrop
 *   anything else -> TactHud_TickCursorDir, then Map_nGlyphPalBase = 34 (0x22370), then:
 *       {0,3,5,7,8,9,0xA,0xB,0xC,0xF}: when Map_bDrawIsoLayer (unk_138E68) is set,
 *           MapView_DrawIsoLayer, then per screen 3 -> UnitRoster, 5 -> CopyStatusText,
 *           7 -> Menu_DrawLabeledPads, 0xA -> modal panel right (record 1103),
 *           0xB -> modal panel left (record 1102), 0xC -> TopBar, 0xF -> ActionButtons.
 *       1 -> InventoryScreen, 2 -> Minimap, 4 -> CommandPads, 6 -> UnitMarkers,
 *       0xD -> StatusBanner, 0x13 -> ConfirmDialog, 0x14/0x15 -> Cydonia pad + panel.
 *       Then the modal dialog when apUiSlotsA[19] (unk_113A64) is set:
 *           UiModal_DrawDialog(width [12], record [13], lines [14], buttons [15], [17]).
 *       Then the fade block: when apUiSlotsA[1] (dword_113A1C) > 0 the whole screen is
 *           dark-blitted (SoftSurf_BlitSrcRect 240x320, key 20); otherwise when
 *           apUiSlotsA[10] (dword_113A40) < 10 the fade pad (unk_156AA0 "enemystp") is
 *           blitted full-screen with alpha 255 when the counter is 0 and 24*(10-counter)
 *           otherwise, and the status stack is drawn when Map_bDrawIsoLayer is set.
 *   return GX_Present().
 *
 * Deviations: the original's UiModal_DrawDialog/StatusMsg_DrawStack read their anim,
 * text-record and stack state out of the Map block; the port passes the sub-envs those
 * ported functions already take. The titles callees receive their own state envs rather
 * than the Map block pointer the original passes as the first argument. The original
 * dereferences the status stack and the fade pad unconditionally (both always exist in
 * the Map block); the port skips those two steps when the env leaves them NULL, which
 * only matters for partial fixtures. */
#ifndef PUFO_RENDERFRAME_H
#define PUFO_RENDERFRAME_H

#include "pufo_common.h"

#include "game/pufo_cursordir.h"  /* CursorDirState */
#include "game/pufo_isodraw.h"    /* IsoDrawCtx, IsoLayerEnv, IsoMinimapEnv, TactCmdPadEnv */
#include "game/pufo_mainmenu.h"   /* MainMenuEnv */
#include "game/pufo_statusmsg.h"  /* StatusStack, StatusDrawHooks */
#include "game/pufo_tacthud.h"    /* the TactHud draw family */
#include "game/pufo_titles.h"     /* TitlesAnimation, TitlesBackdrop */
#include "ui/pufo_control.h"      /* UiControl, UiControlBlitFn */
#include "ui/pufo_modal.h"        /* UiModalAnim, UiModalLayout */
#include "ui/pufo_panel.h"        /* UiPanel, Cydonia */

typedef struct RenderFrameEnv {
    /* Dispatch scalars, the Map-block cells the switch reads. */
    i32 bGeoscapeMode;    /* unk_1139FC @ Map+0x44 */
    i32 nUiScreen;        /* Map_nUiScreen @ Map+0x3A */
    i32 bDrawIsoLayer;    /* unk_138E68 @ Map+0x254B0 */

    /* Hooks for the callees the port does not have yet. Both return values: the original
     * returns Geoscape_Draw's on the geoscape branch and GX_Present's otherwise. */
    i32 (*pfnGeoscapeDraw)(void *pUser); /* Geoscape_Draw (0x8c180), not ported */
    void *pGeoscapeUser;
    i32 (*pfnPresent)(void *pUser);      /* GX_Present (0x27788), mirrored by the shell */
    void *pPresentUser;

    /* The three title/menu screens. */
    TitlesAnimation   *pTitlesAnim;    /* 0x10 */
    TitlesBackdrop    *pTitlesBackdrop;/* 0x12 */
    const MainMenuEnv *pMainMenu;      /* 0x11 */

    /* The tactical default branch: cursor tick, then the per-screen draws. */
    CursorDirState *pCursorDir;
    bool bCursorBusy;
    bool bCursorTrackUnit;
    bool bCursorUnk70115;

    IsoDrawCtx          *pIso;
    const IsoLayerEnv   *pIsoLayer;    /* screens 0,3,5,7,8,9,0xA,0xB,0xC,0xF */
    const IsoMinimapEnv *pMinimap;     /* 2 */
    const TactCmdPadEnv *pCmdPads;     /* 4 */
    TactHudInventoryEnv *pInventory;   /* 1 */
    UiControl              *pMarkerPad;    /* 6, bank widget 104 */
    const TactHudMarkerEnv *pMarkers;
    Soldier *const *apXcom;
    i32 nXcomCount;
    Soldier *const *apEnemy;
    i32 nEnemyCount;

    UiControl              *pRosterPads;   /* 3, bank 32..43 */
    const TactHudRoster    *pRoster;
    UiControl              *pStatusPads;   /* 5 */
    const char             *pszStatus;
    UiControl              *pLabeledPads;  /* 7 */
    UiControl              *pModalPanelPads; /* 0xA / 0xB */
    const u8               *pModalLabelTable;
    i32                     nModalPanelMode;
    UiControl              *pTopBarPads;   /* 0xC, bank 60..88 */
    UiControl              *pActionPads;   /* 0xF, bank 89..97 */
    const TactHudActionGate *pActionGate;

    UiControl              *pBannerPad;    /* 0xD, bank widget 128 */
    const PadImage         *pBannerBackdrop;
    const TactHudBannerState *pBannerState;
    const char             *pszBannerFooter;
    const char             *pszBannerPadLabel;
    UiPanel                *pBannerPanelA;
    UiPanel                *pBannerPanelB;

    UiControl              *pConfirmPad;   /* 0x13, bank widget 129 */
    const PadImage         *pConfirmBackdrop;
    const char             *szConfirmPadLabel;
    const char             *szConfirmTitle;
    const char             *szConfirmLeft;
    const char             *szConfirmRight;
    UiPanel                *pConfirmPanelA;
    UiPanel                *pConfirmPanelB;

    PadImage               *pCydoniaPad;   /* 0x14 / 0x15, 0x15692C */
    UiPanel                *pCydoniaPanel; /* 0x1571EC */

    const u8              *pTextRecs;      /* aTextRecs @ 0xB38D0 */
    const GfxColorEnv     *pColor;
    UiControlBlitFn        pfnBlit;
    UiControlBlitFn        pfnBlitEx;
    void                  *pBlitUser;
    i32                   *pnActionPhaseSave112;
    UiModalAnim           *pModalAnim;

    /* The modal dialog, the apUiSlotsA cells Render_Frame reads at 0x224dc. */
    i32           bModalActive;   /* unk_113A64 = apUiSlotsA[19] */
    i32           nModalWidth;    /* unk_113A48 = apUiSlotsA[12] */
    i32           nModalRec;      /* unk_113A4C = apUiSlotsA[13] */
    i32           nModalLines;    /* unk_113A50 = apUiSlotsA[14] */
    const char   *pszModalBtn1;   /* unk_113A54 = apUiSlotsA[15] */
    const char   *pszModalBtn2;   /* unk_113A5C = apUiSlotsA[17] */
    UiModalLayout *pModalLayout;

    /* The fade block and the status stack. */
    i32                  nFadeCount;  /* dword_113A1C = apUiSlotsA[1] */
    i32                  nFadeIn;     /* dword_113A40 = apUiSlotsA[10] */
    const PadImage      *pFadePad;    /* unk_156AA0, the "enemystp" pad */
    StatusStack         *pStatusStack;
    i32                 *pnStatusOutY;
    const StatusDrawHooks *pStatusHooks;
} RenderFrameEnv;

/* Render_Frame @ 0x22310. Returns the present thunk's value, or the geoscape hook's
 * value on the geoscape branch (which skips the present, as the original does). */
i32 Render_Frame(const RenderFrameEnv *pEnv);

#endif /* PUFO_RENDERFRAME_H */

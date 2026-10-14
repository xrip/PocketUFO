/* pufo_app.c - Win64 entry point and window procedure.
 *
 * Evidence: WinMain @ 0x9fcc4, InitInstance @ 0x9fd48,
 *           PocketUFO_WndProc @ 0xa03ec, App_QuitDisplay @ 0xa0388.
 *
 * This shell opens the window, loads the startup assets, opens the display
 * surface, and runs the message loop. The game state machine and the per-screen
 * render call belong to the UI/strategy tasks; until those land this shows the
 * loaded 240x320 surface so the data path is provably live.
 *
 * Ported from the original:
 *   - single instance via FindWindowW(class "POCKET_UFO", title "POCKET UFO")
 *   - RegisterClassW with CS_VREDRAW|CS_HREDRAW
 *   - WS_VISIBLE window sized to the display
 *   - accelerator table load + TranslateAcceleratorW
 *
 * Deliberately dropped (no Win64 equivalent), with reasons:
 *   - the CE idiom SetForegroundWindow(hWnd | 1)
 *   - ExtEscape(GETVFRAMEPHYSICAL) raw-framebuffer probe
 *   - GX display-mode / pitch-sign handling (the DIB is always top-down RGB565)
 *   - the unsupported-resolution early quit: that existed only because GAPI
 *     could not scale; Win64 letterboxes instead.
 */
#include "pufo_common.h"
#include "game/pufo_boot.h"
#include "game/pufo_font.h"     /* PufoFont, the shell owns the fontSys block */
#include "game/pufo_mainmenu.h" /* GameUI_InitStartup / MainMenu_Draw / MainMenu_TickRowKeys */
#include "game/pufo_options.h"  /* Options_LoadFromRegistry */
#include "game/pufo_timer.h"    /* Timer_SetWindow, the ids the WM_TIMER branch dispatches on */
#include "platform/pufo_gx.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"    /* UiControl_LoadPads, the start controls' pad loader */
#include "util/pufo_rand.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

#define PUFO_CLASS_NAME  L"POCKET_UFO" /* ClassName @ 0xac6a8 */
#define PUFO_WINDOW_NAME L"POCKET UFO" /* WindowName @ 0xa5000 */

static HWND       g_hWndMain = NULL;
static GxSurface  g_gx;
static BootAssets g_assets;
static bool       g_bAssetsLoaded;
static char       g_szLangPath[MAX_PATH];
static char       g_szMainPath[MAX_PATH];

/* The start-menu state GameUI_InitStartup builds and MainMenu_Draw reads. The shell owns the
 * blocks the original keeps in its global bank: the 15 menu controls (0xAD4F0), the standalone
 * control (0x1571B4), the backdrop glyph (0x1568D8), the empty-button pad (0x1568EC), the seven
 * row keys (0x1568F0), the seven disabled bytes (0x156AB0), the seven key sources (0x1571C8),
 * the list mode (0x157188) and the ui screen (Map_nUiScreen). The font is the module global
 * g_fontSys (fontSys @ 0x158268): Text_DrawString reaches it directly, so the shell must not
 * keep a private copy. */
static PufoOptions g_options;
static UiControl   g_aMenuCtl[15];
static UiControl   g_startCtl;
static u8         *g_pBgGlyph;
static u8         *g_pEmptyPad;
static i32         g_aRowKeys[7];
static u8          g_abRowDisabled[7];
static i32         g_aRowKeySrc[7];
static i32         g_nListMode;
static i32         g_nUiScreen;
static i32         g_nAnimPhase;  /* unk_1139EF, the 0xC352 anim counter */
static i32         g_nKeyAccum;   /* unk_161A38, write-only residue of the flicker */
static i32         g_nPhaseSave112;
static GfxColorEnv g_color;
static bool        g_bStartupUi;

static void App_PresentLoadedSurface(void);

static bool App_LoadPads(UiControl *pCtl, const char *psz0, const char *psz1,
                         const char *psz2, void *pUser)
{
    (void)pUser;
    UiControl_LoadPads(pCtl, psz0, psz1, psz2); /* UiControl_LoadPads @ 0x1a604 */
    return true;
}

static void App_BlitPad(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    PadImage_Blit(pPad);
}

static void App_BlitPadEx(const PadImage *pPad, void *pUser)
{
    (void)pUser;
    PadImage_BlitEx(pPad, 255u);
}

/* GameUI_InitStartup @ 0x38a28 for the shell. The eleven menu click handlers and the
 * empty-button handler are left NULL here: their hook blocks (StartUi_Shutdown, Geoscape_InitUI,
 * SaveSlot_FormatList, Titles_Init) belong to the menu-button and geoscape steps, and the port's
 * handlers tolerate a missing hook. */
static void App_InitStartupUi(void)
{
    GameUiStartupEnv env;

    Options_LoadFromRegistry(&g_options);
    Timer_SetWindow(g_hWndMain);

    memset(&env, 0, sizeof(env));
    memset(g_aMenuCtl, 0, sizeof(g_aMenuCtl));
    memset(&g_startCtl, 0, sizeof(g_startCtl));
    env.pFont          = &g_fontSys; /* fontSys @ 0x158268 — the same object Text_DrawString reads */
    env.pStartCtl      = &g_startCtl;
    env.ppBgGlyph      = &g_pBgGlyph;
    env.ppEmptyBtnPad  = &g_pEmptyPad;
    env.anRowKeys      = g_aRowKeys;
    env.abRowDisabled  = g_abRowDisabled;
    env.anRowKeySrc    = g_aRowKeySrc;
    env.pnListMode     = &g_nListMode;
    env.pnUiScreen     = &g_nUiScreen;
    env.pOptions       = &g_options;
    env.pfnLoadPads    = App_LoadPads;
    GameUI_InitStartup(&env);
    g_bStartupUi = (g_nUiScreen == 17);
}

/* The screen-17 branch of Render_Frame (0x22310, not ported; its case 0x11 calls
 * MainMenu_Draw at 0x225E4). The shell reaches the menu branch directly until the
 * dispatcher is ported. */
static void App_DrawFrame(void)
{
    MainMenuEnv mm;

    if (!g_bStartupUi || g_nUiScreen != 17) {
        return;
    }
    memset(&mm, 0, sizeof(mm));
    mm.pBgGlyph             = g_pBgGlyph;
    mm.nListMode            = g_nListMode;
    mm.anRowKeys            = g_aRowKeys;
    mm.abRowDisabled        = g_abRowDisabled;
    mm.pSlotLabels          = NULL; /* modes 2/3 only */
    mm.pStartCtl            = &g_startCtl;
    mm.pTextRecs            = g_assets.pTextRecs;
    mm.pnActionPhaseSave112 = &g_nPhaseSave112;
    mm.pModalAnim           = NULL; /* mode 3 only */
    mm.pColor               = &g_color;
    mm.pfnCtlBlit           = App_BlitPad;
    mm.pfnCtlBlitEx         = App_BlitPadEx;
    MainMenu_Draw(&mm);
    App_PresentLoadedSurface();
    (void)Gx_Present(&g_gx);
}

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void App_PresentLoadedSurface(void);

/* Builds the exe-directory-relative asset paths, as InitInstance @ 0x9fd48 does:
 * GetModuleFileNameW, strip to the last backslash, then append lang.dat and
 * main.dat. The originals are szAppDir @ 0x1883b0 and szPathBuf @ 0x188538. */
static void App_BuildAssetPaths(void)
{
    WCHAR  awcPath[MAX_PATH];
    char   szDir[MAX_PATH];
    DWORD  cch = GetModuleFileNameW(NULL, awcPath, MAX_PATH);
    DWORD  i;
    size_t nDir = 0;

    g_szLangPath[0] = '\0';
    g_szMainPath[0] = '\0';
    if (cch == 0 || cch >= MAX_PATH) {
        return;
    }

    /* Keep everything up to and including the last backslash. */
    for (i = 0; i < cch; ++i) {
        if (awcPath[i] == L'\\') {
            nDir = i + 1u;
        }
    }
    awcPath[nDir] = L'\0';
    szDir[0] = '\0';
    (void)WideCharToMultiByte(CP_ACP, 0, awcPath, -1, szDir,
                              (int)sizeof(szDir), NULL, NULL);
    /* Same directory, two file names. snprintf keeps the bounds explicit. */
    (void)snprintf(g_szLangPath, sizeof(g_szLangPath), "%s%s", szDir, "lang.dat");
    (void)snprintf(g_szMainPath, sizeof(g_szMainPath), "%s%s", szDir, "main.dat");
}

/* Copy the loaded 240x320 logical surface onto the window DIB so the data path
 * is visible: the original blits the same surface in GX_PresentFrame @ 0x9ea7c. */
static void App_PresentLoadedSurface(void)
{
    SoftSurf *pSurface = g_assets.pSurface;
    int       y;

    if (pSurface == NULL || pSurface->pPixels == NULL || g_gx.pBits == NULL) {
        return;
    }
    /* GX_PresentFrame @ 0x9ea7c arms the blit target (Render_SetTarget @ 0x9faa4 ->
     * Blit_SetTargetParams @ 0xa39ac) before blitting the soft surface. The target's pixel
     * mode is what SoftSurf_BlitGlyph_Inner @ 0xa41d4 gates on (blitState+0x20), so without
     * this call no glyph ever draws. The 240x320/2bpp constants are the original's own. */
    Render_SetTarget(240, 320, 2, g_gx.cbxPitch, g_gx.cbyPitch, g_gx.pBits);
    for (y = 0; y < (int)g_gx.cyHeight && y < pSurface->nHeight; ++y) {
        const u8 *pSrc = pSurface->pPixels + (size_t)y * (size_t)pSurface->nWidth * 2u;
        u8       *pDst = g_gx.pBits + (size_t)y * (size_t)g_gx.cbyPitch;
        size_t    cb   = (size_t)pSurface->nWidth * 2u;

        if (cb > (size_t)g_gx.cxWidth * 2u) {
            cb = (size_t)g_gx.cxWidth * 2u;
        }
        memcpy(pDst, pSrc, cb);
    }
}

static WCHAR g_wszDumpPath[MAX_PATH];

/* `--dump-frame <path>` on the command line. Returns the path or NULL. */
static LPCWSTR App_FindDumpFlag(LPCWSTR pwszCmd)
{
    static const WCHAR awcFlag[] = L"--dump-frame";
    int i = 0;

    if (pwszCmd == NULL) {
        return NULL;
    }
    pwszCmd = wcsstr(pwszCmd, awcFlag);
    if (pwszCmd == NULL) {
        return NULL;
    }
    pwszCmd += (int)(sizeof(awcFlag) / sizeof(WCHAR)) - 1;
    while (*pwszCmd == L' ') {
        ++pwszCmd;
    }
    while (*pwszCmd != L'\0' && *pwszCmd != L' ' && i < MAX_PATH - 1) {
        g_wszDumpPath[i++] = *pwszCmd++;
    }
    g_wszDumpPath[i] = L'\0';
    return (i == 0) ? NULL : g_wszDumpPath;
}

/* Writes the logical 240x320 RGB565 surface as a binary PPM (P6). A debug hook for
 * `pocketufo --dump-frame <path>`: the rendered frame can then be inspected or compared without
 * a screen grab. Not part of the original — the original presents through GX. */
static void App_DumpFramePpm(LPCWSTR pwszPath)
{
    const SoftSurf *p = g_assets.pSurface;
    HANDLE hFile;
    char   szHeader[64];
    char   aRow[240 * 3]; /* the logical surface is always 240 wide (SoftSurf_AllocPixels(240,320,2)) */
    int    nHeader;
    DWORD  cbWritten;

    if (p == NULL || p->pPixels == NULL) {
        return;
    }
    hFile = CreateFileW(pwszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    nHeader = snprintf(szHeader, sizeof(szHeader), "P6\n%d %d\n255\n", p->nWidth, p->nHeight);
    if (nHeader > 0) {
        (void)WriteFile(hFile, szHeader, (DWORD)nHeader, &cbWritten, NULL);
    }
    for (int y = 0; y < p->nHeight; ++y) {
        const u16 *pRow = (const u16 *)(p->pPixels + (size_t)y * (size_t)p->nWidth * 2u);
        for (int x = 0; x < p->nWidth; ++x) {
            const u16 v = pRow[x];
            aRow[3 * x + 0] = (char)(((v >> 11) & 0x1Fu) * 255 / 31);
            aRow[3 * x + 1] = (char)(((v >> 5)  & 0x3Fu) * 255 / 63);
            aRow[3 * x + 2] = (char)((v & 0x1Fu) * 255 / 31);
        }
        (void)WriteFile(hFile, aRow, (DWORD)(p->nWidth * 3), &cbWritten, NULL);
    }
    (void)CloseHandle(hFile);
}

/* Fill the logical surface with a single RGB565 colour. Fallback when the assets
 * could not be loaded, so the window still proves the display path. */
static void App_ClearSurface(u16 colour)
{
    int y;

    for (y = 0; y < (int)g_gx.cyHeight; ++y) {
        u16 *pRow = (u16 *)(g_gx.pBits + (size_t)y * (size_t)g_gx.cbyPitch);
        int  x;

        for (x = 0; x < (int)g_gx.cxWidth; ++x) {
            pRow[x] = colour;
        }
    }
}

static bool App_InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSW wc;
    HWND      hWnd;

    /* Single instance, matching InitInstance @ 0x9fd48. The original raises
     * the existing window; that is safe to keep on Win64. */
    hWnd = FindWindowW(PUFO_CLASS_NAME, PUFO_WINDOW_NAME);
    if (hWnd != NULL) {
        (void)SetForegroundWindow(hWnd);
        return false;
    }

    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_VREDRAW | CS_HREDRAW; /* original style == 3 */
    wc.lpfnWndProc   = PocketUFO_WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = PUFO_CLASS_NAME;
    if (RegisterClassW(&wc) == 0) {
        return false;
    }

    /* The original sizes the WINDOW so the client matches the display. CreateWindowExW's
     * width/height are the window size, so the frame has to be added first; without this the
     * 240x320 surface is cropped by the title bar and borders. */
    {
        RECT rc = { 0, 0, PUFO_SURF_W, PUFO_SURF_H };
        DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        (void)AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);
        hWnd = CreateWindowExW(0, PUFO_CLASS_NAME, PUFO_WINDOW_NAME,
                               dwStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               NULL, NULL, hInstance, NULL);
    }
    if (hWnd == NULL) {
        return false;
    }
    g_hWndMain = hWnd;

    if (!Gx_Open(&g_gx, hWnd, PUFO_SURF_W, PUFO_SURF_H)) {
        printf("pocketufo: display surface failed to open\n");
        fflush(stdout);
        DestroyWindow(hWnd);
        g_hWndMain = NULL;
        return false;
    }

    Game_SeedRng();

    /* Assets_LoadStartup @ 0x382d0. The original resolves the paths from the exe
     * directory in InitInstance and loads here; a failure is not fatal there
     * either (it leaves the game with no assets), but it is reported here. */
    App_BuildAssetPaths();
    {
        const char *const apszArchives[PUFO_ARCHIVE_COUNT] = {
            g_szLangPath, g_szMainPath
        };

        g_bAssetsLoaded = Boot_LoadStartup(&g_assets, apszArchives);

        /* DEVIATION: the original's InitInstance @ 0x9fd48 reports nothing, and on WinCE that was fine
         * because a missing asset pack still left a visible window. On Win64 a build with no assets and a
         * build that is merely between frames look the same from the outside, so the port states what it
         * found. This is the only way to tell a good launch from a broken one without looking at the
         * screen. */
        printf("pocketufo: assets %s (lang.dat=%s, main.dat=%s)\n",
               g_bAssetsLoaded ? "loaded" : "NOT FOUND", g_szLangPath, g_szMainPath);
        fflush(stdout);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return true;
}

static void App_QuitDisplay(void)
{
    if (g_bAssetsLoaded) {
        Boot_Shutdown(&g_assets);
        g_bAssetsLoaded = false;
    }
    Gx_Close(&g_gx);
    PostQuitMessage(0);
}

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;

        (void)BeginPaint(hWnd, &ps);
        (void)Gx_Present(&g_gx);
        (void)EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; /* the surface covers the client area */
    case WM_SIZE:
        if (g_gx.pBits != NULL) {
            (void)Gx_Present(&g_gx);
        }
        return 0;
    case WM_TIMER:
        /* PocketUFO_WndProc @ 0xA0944's timer dispatch. 0xC351 is the render tick (the original
         * calls Render_Frame); 0xC352 is the anim tick, which advances the phase counter
         * (0xA0978..0xA098C: >=7 wraps to 0) and runs the screen-17 row-key flicker (0xA0AC8). */
        if (wParam == PUFO_TIMER_ANIM) {
            if (g_nAnimPhase >= 7) {
                g_nAnimPhase = 0; /* 0xA0988 */
            } else {
                ++g_nAnimPhase;   /* 0xA098C */
            }
            if (g_nUiScreen == 17) {
                MainMenu_TickRowKeys(g_aRowKeys, g_aRowKeySrc, g_nAnimPhase, &g_nKeyAccum);
            }
        } else if (wParam == PUFO_TIMER_RENDER) {
            App_DrawFrame();
        }
        return 0;
    case WM_KEYDOWN:
        /* Scaffold: Esc quits, mirroring the original's escape path. */
        if (wParam == VK_ESCAPE) {
            App_QuitDisplay();
            return 0;
        }
        break;
    case WM_DESTROY:
        App_QuitDisplay();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    MSG            msg;
    HACCEL         hAccel;
    unsigned short colour = 0x0000u; /* black in RGB565 */

    LPCWSTR pszDumpPath;

    (void)hPrevInstance;
    pszDumpPath = App_FindDumpFlag(lpCmdLine);

    if (!App_InitInstance(hInstance, nCmdShow)) {
        return 0;
    }

    /* Match WinMain @ 0x9fcc4: accelerators are loaded unconditionally. A
     * missing table is not fatal there either. */
    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(1));

    /* GameUI_InitStartup @ 0x38a28 runs after the assets are in (the original reaches it from
     * the title/menu path once Boot_LoadStartup's archives are open). It loads the fonts, the
     * startg/menu palettes, the backdrop glyph and the "tact" samples, sets the ui screen to 17,
     * and arms the 0xC351/0xC352 timers through StartTimers_B. */
    if (g_bAssetsLoaded) {
        App_InitStartupUi();
        App_DrawFrame();
        if (pszDumpPath != NULL) {
            /* Debug path: run the anim clock for about a second (40 x 25 ms ticks, the same
             * body WM_TIMER 0xC352 runs) so the row keys reach their steady value, render one
             * frame, write it out, and leave. */
            for (int i = 0; i < 40; ++i) {
                if (g_nAnimPhase >= 7) {
                    g_nAnimPhase = 0;
                } else {
                    ++g_nAnimPhase;
                }
                if (g_nUiScreen == 17) {
                    MainMenu_TickRowKeys(g_aRowKeys, g_aRowKeySrc, g_nAnimPhase, &g_nKeyAccum);
                }
            }
            App_DrawFrame();
            App_DumpFramePpm(pszDumpPath);
            Boot_Shutdown(&g_assets);
            g_bAssetsLoaded = false;
            Gx_Close(&g_gx);
            return 0;
        }
    } else {
        App_ClearSurface(colour);
        (void)Gx_Present(&g_gx);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (hAccel == NULL || TranslateAcceleratorW(msg.hwnd, hAccel, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

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
#include "platform/pufo_gx.h"
#include "render/pufo_render.h"
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

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

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

    hWnd = CreateWindowExW(0, PUFO_CLASS_NAME, PUFO_WINDOW_NAME,
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           PUFO_SURF_W, PUFO_SURF_H,
                           NULL, NULL, hInstance, NULL);
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

    (void)hPrevInstance;
    (void)lpCmdLine;

    if (!App_InitInstance(hInstance, nCmdShow)) {
        return 0;
    }

    /* Match WinMain @ 0x9fcc4: accelerators are loaded unconditionally. A
     * missing table is not fatal there either. */
    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(1));

    /* Show the loaded surface when the assets came in, otherwise a flat fill so
     * the display path is still visibly working. */
    if (g_bAssetsLoaded) {
        App_PresentLoadedSurface();
    } else {
        App_ClearSurface(colour);
    }
    (void)Gx_Present(&g_gx);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (hAccel == NULL || TranslateAcceleratorW(msg.hwnd, hAccel, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

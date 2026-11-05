#include "ui/pufo_uigeom.h"

static int g_nUiOffX;
static int g_nUiOffY;

void PufoUi_SetGeometry(int nOffX, int nOffY)
{
    g_nUiOffX = nOffX;
    g_nUiOffY = nOffY;
}

int PufoUi_MapX(int nX)
{
    return nX + g_nUiOffX;
}

int PufoUi_MapY(int nY)
{
    return (nY >= PUFO_UI_BOTTOM_ANCHOR_Y) ? nY + g_nUiOffY : nY;
}

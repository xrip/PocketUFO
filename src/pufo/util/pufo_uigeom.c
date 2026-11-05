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

int PufoUi_OffY(void)
{
    return g_nUiOffY;
}

int PufoUi_MapY(int nY)
{
    return (nY >= PUFO_UI_BOTTOM_ANCHOR_Y) ? nY + g_nUiOffY : nY;
}

/* Center-anchored windows (the tact main menu stack): the whole window keeps
 * its shape and rides at the face centre: the shift is half offY (portrait 0). */
int PufoUi_MapCentreY(int nY)
{
    return nY + g_nUiOffY / 2;
}

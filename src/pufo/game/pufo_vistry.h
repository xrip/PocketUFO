/* pufo_vistry.h - the visibility ray table the tactical layer walks.
 *
 * Evidence: Tact_BuildVisRayTable @ 0x37ae4 (the builder), dword_B2278 @ 0xb2278
 * (120 perimeter rays, 40 bytes each), byte_AD720 @ 0xad720 (the step table,
 * 8 passes x 1860 bytes). Readers: Map_ComputeFov @ 0x2deac and
 * Map_ScanBlastFootprint @ 0x32cd4.
 *
 * byte_AD720 is .data initialised to 0xFF: it is runtime data, built once per
 * mission. That is why dumping it out of the IDB yields nothing but 0xFF.
 *
 * Step record, four bytes, from the addressing in both readers:
 *   +0 dominant axis value at this step, or 96 for "the ray ends here"
 *   +1 the other axis value
 *   +2 wall A flag: 0 unchanged, 1 the other axis increased, 2 it decreased
 *   +3 wall B flag, same encoding
 * The next step's two axis values are the following record's +0 and +1, which is
 * why Map_ComputeFov reads them as v16[4] and v16[5].
 */
#ifndef PUFO_VISTRY_H
#define PUFO_VISTRY_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

#define PUFO_VIS_RAY_COUNT 120
#define PUFO_VIS_PASS_COUNT 8
#define PUFO_VIS_STEP_COUNT 15
#define PUFO_VIS_RAYS_PER_PASS 31   /* active rays in one pass */
#define PUFO_VIS_GROUP_BYTES 1860   /* 31 rays * 15 steps * 4 bytes */
#define PUFO_VIS_TABLE_BYTES (PUFO_VIS_PASS_COUNT * PUFO_VIS_GROUP_BYTES)
/* A row holds exactly fifteen steps. The walk writes step 0..14 and then breaks
 * when the dominant axis reaches its limit, so no row overruns into the next. */
#define PUFO_VIS_RAY_END 96         /* the end-of-ray sentinel byte */
#define PUFO_VIS_RAY_LIMIT 15.0     /* _gtd against 1076756480 << 32 */

/* One of the 120 rays: the direction, then one flag per pass. aPass[p] is
 * non-zero when this ray is walked in pass p. The original stores the flags in
 * the same record, which is why the builder's walk loop tests dword 2 + p. */
typedef struct VisRay {
    i32 nDx;
    i32 nDy;
    i32 aPass[PUFO_VIS_PASS_COUNT];
} VisRay;

/* Build the ray list and the step table into caller-owned storage. apRays is
 * PUFO_VIS_RAY_COUNT records, paSteps is PUFO_VIS_TABLE_BYTES bytes.
 *
 * Pass p walks rays (15 * p + k) mod 120 for k = 0..30, which is 31 rays: the
 * builder seeds aPass[p] exactly that way. Map_ComputeFov uses passes 0, 2, 4, 6
 * only; the other four are for Map_ScanBlastFootprint. */
void Tact_BuildVisRayTable(VisRay *apRays, u8 *paSteps);

/* ## Map_ComputeFov @ 0x2deac
 *
 * Eight parameters in the original: R0 pMap, R1 a2, R2 a3, R3 a4, then four
 * stack arguments at calleeSP+0x48/0x4C/0x50/0x54 = nOriginY, nLayer, nRange, a8
 * (callee frame size 0x48).
 *
 * It walks the ray table for passes 0, 2, 4 and 6 (the original drives this with
 * a global counter, byte_10DBB4, starting at 0 and stepping by 2 while the next
 * value stays under 8), and writes a 31 x 31 field of four u16 per entry, indexed
 * by an offset pair in -15..15. In the Map the field starts at 77788 and the
 * whole cleared region is Map+73948 for 7688 bytes.
 *
 * Slot meaning, from the offsets the original uses:
 *   slot 0  written together with slot 3 whenever slot 3 is below the running cost
 *   slot 1  wall A cost, written by the flag-2 branch of byte 2
 *   slot 2  wall B cost, written by the flag-2 branch of byte 3
 *   slot 3  the running cost itself
 *
 * The four flags in a step record decide which cell a wall belongs to: flag 2
 * means the wall is on the cell the ray is leaving, flag 1 means it is on the
 * cell the ray is entering. The two branches are not symmetric and the original
 * is not either: the flag-2 branch records the cost before subtracting the wall,
 * the flag-1 branch subtracts first and records the reduced value.
 *
 * A wall only costs range when
 *   sprite[22] > cell->bUnk17 (or == 255)  and
 *   sprite[42] == 0 or the cell's bFlags29 bit 2 is clear
 * which is the same pair of conditions Map_TraceVoxelOccupancy uses to decide
 * whether a wall still blocks.
 */
#define PUFO_FOV_GRID   31
#define PUFO_FOV_SLOTS   4
#define PUFO_FOV_OFFSET  15   /* the grid is indexed -15..15 */
#define PUFO_FOV_COST_FAST  8.0    /* a8 == 1, or the weapon's uSortKey == 50 */
#define PUFO_FOV_COST_SLOW 20.0
#define PUFO_FOV_WEAPON_CLASS_FAST 50
#define PUFO_HIT_WINDOW_SIDE 41    /* same span as PUFO_SPLASH_SPAN */

typedef struct FovField {
    u16 aSlot[PUFO_FOV_GRID * PUFO_FOV_GRID][PUFO_FOV_SLOTS];
} FovField;

/* Entry accessor for an offset pair in -15..15. */
size_t Fov_EntryIndex(i32 nOffX, i32 nOffY);

/* Map_ComputeFov @ 0x2deac.
 *
 * nColBase and nRowBase are the original's a2 and a3: the fold lands entry
 * (offX, offY) on hit-window (row = nRowBase + 20 + offY, col = nColBase + 20 +
 * offX), and the walk reads cell (nOriginX + offX, nOriginY + offY).
 *
 * paWindow must hold PUFO_HIT_WINDOW_SIDE^2 * 4 dwords. Only the low half of each
 * record dword is touched, which is what the original's u16 read-modify-write
 * does; the upper half is left alone.
 *
 * nCostMode is the original's a8 and nWeaponSortKey stands in for the
 * *(u16 *)(pCtxBase.nFireArg6 + 200) read, which is ObjectRecFull.uSortKey. */
void Map_ComputeFov(FovField *pFov, u32 *paWindow,
                    const MapGeom *pGeom, MapCellLookup pfnLookup, void *pUser,
                    const u8 *paSteps,
                    i32 nColBase, i32 nRowBase, i32 nOriginX, i32 nOriginY,
                    i32 nLayer, i32 nRange, i32 nCostMode, u16 nWeaponSortKey);

#endif /* PUFO_VISTRY_H */

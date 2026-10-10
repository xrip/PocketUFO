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

#include "pufo_common.h"

#define PUFO_VIS_RAY_COUNT 120
#define PUFO_VIS_PASS_COUNT 8
#define PUFO_VIS_STEP_COUNT 15
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

#endif /* PUFO_VISTRY_H */

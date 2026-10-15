/* Optional observation of FIFO -> decoder decisions. Not a rendering path.
 * Called under the FIFO serialization lock. Gate absence is the default.
 * Reads are sequential; this is not a GPU completion or presentation witness. */
#ifndef PS3RECOMP_GCM_DRAW_TRACE_H
#define PS3RECOMP_GCM_DRAW_TRACE_H
#include "rsx_commands.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void gcm_draw_trace(u32 get, u32 subch, u32 method, u32 data,
                           int result, const rsx_state* state)
{
    static int configured, open_gate, exhausted;
    static const char* path;
    static unsigned polls, records;
    const u32 m = method & 0x1FFCu;
    if (!configured) { path = getenv("GCM_SUBCH_GATE"); configured = 1; }
    if (!path || !path[0] || exhausted) return;
    /* Draw/inline family, render targets, program and vertex fetch bindings. */
    if (!((m >= 0x1808 && m <= 0x182C) || m == 0x220 ||
          m == 0x210 || m == 0x214 || m == 0x218 || m == 0x288 || m == 0x28C ||
          m == 0x8E4 || m == 0x1D94 || m == 0x1FF0 || m == 0x1FF4 ||
          (m >= 0x1680 && m < 0x16C0) || (m >= 0x1740 && m < 0x1780))) return;
    if ((polls++ & 255u) == 0) {
        FILE* gate = fopen(path, "rb");
        open_gate = gate != NULL;
        if (gate) { if (fclose(gate)) abort(); }
        else if (errno != ENOENT) { perror("GCM draw trace gate"); abort(); }
    }
    if (!open_gate) return;
    if (records == 65536) {
        exhausted = 1;
        if (printf("[GCM_DRAW_LIMIT] records=65536\n") < 0 || fflush(stdout)) abort();
        return;
    }
    ++records;
    if (printf("[GCM_DRAW] n=%u get=%08X subch=%u method=%04X data=%08X result=%d "
               "rt=%08X,%08X,%08X,%08X target=%X fp=%08X primitive=%u active=%d\n",
               records, get, subch, m, data, result,
               state->surface_color_offset[0], state->surface_color_offset[1],
               state->surface_color_offset[2], state->surface_color_offset[3],
               state->color_target, state->shader_program,
               state->primitive_type, state->in_begin_end) < 0) abort();
}
#endif

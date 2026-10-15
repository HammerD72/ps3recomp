/* Authored metadata fixture; exercises the actual optional trace writer. */
#include "../libs/video/gcm_draw_trace.h"
#include <string.h>
int main(int argc, char** argv)
{
    rsx_state state;
    if (argc != 2) return 2;
    memset(&state, 0, sizeof(state));
    state.surface_color_offset[0] = 0x10000;
    state.color_target = 1;
    state.shader_program = 0x123400;
    state.primitive_type = 5;
    state.in_begin_end = 1;
#ifdef _WIN32
    if (_putenv_s("GCM_SUBCH_GATE", argv[1])) return 3;
#else
    if (setenv("GCM_SUBCH_GATE", argv[1], 1)) return 3;
#endif
    /* Unknown inline and accepted array results must both survive logging.
     * The writer observes caller results; this does not test the decoder. */
    gcm_draw_trace(0x400, 0, 0x1818, 0x3F800000, -1, &state);
    gcm_draw_trace(0x410, 0, 0x1814, 0x03000000, 0, &state);
    gcm_draw_trace(0x420, 0, 0x1F00, 0, 0, &state); /* unselected */
    return 0;
}

#include <stdio.h>
#include <string.h>
#include "rsx_texture_layout.h"

/* Independent format contract: RPCS3 f94e9f8647aeae07ea064dbafe407a41a922f485,
 * Emu/RSX/VK/VKFormats.cpp get_component_mapping(). ARGB crossbar order. */
int main(void)
{
    const struct { u32 format, control; u8 want[4]; } cases[] = {
        {0x9f, 0xaae4, {0,1,0,1}}, /* XY16F: R,G,R,G, not A,R,G,B */
        {0xbf, 0x29e4, {RSX_REMAP_ONE,1,0,RSX_REMAP_ZERO}},
        {0x9f, 0xaa1b, {1,0,1,0}},
        {0x9f, 0,      {0,1,0,1}}, /* existing unset policy */
        {0x85, 0xaae4, {3,0,1,2}}, /* ordinary ARGB unchanged */
        {0x8b, 0xaae4, {1,0,1,0}}, /* integer two-channel unchanged */
    };
    unsigned failures = 0;
    for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) {
        u8 actual[4];
        rsx_texture_component_remap(cases[i].control, cases[i].format, actual);
        int ok = !memcmp(actual, cases[i].want, sizeof actual);
        printf("case %u format=%02x control=%04x ARGB=%u,%u,%u,%u %s\n",
            i, cases[i].format, cases[i].control, actual[0], actual[1],
            actual[2], actual[3], ok ? "PASS" : "FAIL");
        failures += !ok;
    }
    return failures ? 1 : 0;
}

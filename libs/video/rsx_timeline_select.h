#ifndef RSX_TIMELINE_SELECT_H
#define RSX_TIMELINE_SELECT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
typedef struct {
    uint32_t target, count, by_fp, values[8];
} rsx_timeline_selection;
/* One target, then either ordered decimal operations or ordered fp:HEX
 * identities. Never mix modes. No payload or title knowledge lives here. */
static int rsx_timeline_parse(const char* text, uint32_t max_ops,
                              rsx_timeline_selection* out)
{
    rsx_timeline_selection s = {0};
    const char* p = text;
    char* end;
    while (isspace((unsigned char)*p)) ++p;
    if (!isxdigit((unsigned char)*p)) return 0;
    errno = 0;
    unsigned long v = strtoul(p, &end, 16);
    if (errno || v > UINT32_MAX || !v || (*end && !isspace((unsigned char)*end))) return 0;
    s.target = (uint32_t)v; p = end;
    for (;;) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (s.count == 8) return 0;
        uint32_t mode = strncmp(p, "fp:", 3) == 0;
        if (s.count && mode != s.by_fp) return 0;
        s.by_fp = mode;
        if (mode) p += 3;
        if (!(mode ? isxdigit((unsigned char)*p) : isdigit((unsigned char)*p))) return 0;
        errno = 0; v = strtoul(p, &end, mode ? 16 : 10);
        if (errno || v > UINT32_MAX || (*end && !isspace((unsigned char)*end))) return 0;
        if (mode ? !v : (v >= max_ops || (s.count && v <= s.values[s.count-1]))) return 0;
        s.values[s.count++] = (uint32_t)v; p = end;
    }
    if (!s.count) return 0;
    *out = s; return 1;
}
/* Resolve all FP selectors against ONE original submission list. Missing
 * identities leave the request pending, never admit a partial capture. */
static int rsx_timeline_resolve(const rsx_timeline_selection* s,
                                const uint32_t* programs, uint32_t count,
                                uint32_t operations[8])
{
    uint32_t temp[8], start = 0;
    if (!s->by_fp) { memcpy(operations, s->values, s->count*sizeof(uint32_t)); return 1; }
    for (uint32_t k = 0; k < s->count; ++k) {
        uint32_t i = start;
        while (i < count && programs[i] != s->values[k]) ++i;
        if (i == count) return 0;
        temp[k] = i; start = i+1;
    }
    memcpy(operations, temp, s->count*sizeof(uint32_t)); return 1;
}
#endif

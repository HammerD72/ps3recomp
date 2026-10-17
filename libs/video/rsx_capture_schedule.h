/* Opt-in frame capture scheduling; no guest or rendering state is changed. */
#ifndef PS3RECOMP_RSX_CAPTURE_SCHEDULE_H
#define PS3RECOMP_RSX_CAPTURE_SCHEDULE_H
#include <stdint.h>

#define RSX_CAPTURE_BURST_MAX 120u
#define RSX_CAPTURE_SCHEDULED 1u
#define RSX_CAPTURE_BURST 2u

typedef struct rsx_capture_schedule {
    uint32_t start, count, stride, done, index;
    uint32_t burst_remaining;
    int burst_seen;
    int presented_mode;
    uint64_t presented_frames;
} rsx_capture_schedule;

static inline void rsx_capture_schedule_init(rsx_capture_schedule* s,
    uint32_t start, uint32_t count, uint32_t stride)
{
    *s = (rsx_capture_schedule){0};
    s->start = start;
    s->count = count;
    s->stride = stride ? stride : 1;
}

/* A marker can arm only one burst per process, even after exhaustion. */
static inline int rsx_capture_schedule_arm(rsx_capture_schedule* s, uint32_t count)
{
    if (s->burst_seen || !count || count > RSX_CAPTURE_BURST_MAX) return 0;
    s->burst_seen = 1;
    s->burst_remaining = count;
    return 1;
}

/* Switch once to a bounded late phase; preserve capture indices and burst state. */
static inline int rsx_capture_schedule_presented(rsx_capture_schedule* s,
    uint32_t count, uint32_t stride)
{
    if (s->presented_mode || !count || count > RSX_CAPTURE_BURST_MAX || !stride || stride > 10000u) return 0;
    s->presented_mode = 1;
    s->presented_frames = 0;
    s->start = 1;
    s->done = 0;
    s->count = count;
    s->stride = stride;
    return 1;
}

/* Overlap produces one capture and spends both budgets. Empty/offscreen-only
 * batches do not consume the presented-frame burst. Indices never restart. */
static inline unsigned rsx_capture_schedule_next(rsx_capture_schedule* s,
    uint64_t frame, int has_draws, int presenting, uint32_t* index)
{
    if (!has_draws) return 0;
    if (s->presented_mode) {
        if (!presenting) return 0;
        frame = ++s->presented_frames;
    }
    unsigned reason = 0;
    if (s->done < s->count && frame >= (uint64_t)s->start + (uint64_t)s->done * s->stride)
        reason |= RSX_CAPTURE_SCHEDULED;
    if (s->burst_remaining && presenting) reason |= RSX_CAPTURE_BURST;
    if (!reason) return 0;
    if (reason & RSX_CAPTURE_SCHEDULED) ++s->done;
    if (reason & RSX_CAPTURE_BURST) --s->burst_remaining;
    *index = s->index++;
    return reason;
}
#endif

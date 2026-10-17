#include <stdio.h>
#include <string.h>
#include "rsx_capture_schedule.h"
static int checks, failures;
static void check(int ok, const char* name) { ++checks; if (!ok) { ++failures; printf("FAIL %s\n", name); } }
int main(int argc, char** argv) {
    int legacy = argc > 1 && !strcmp(argv[1], "--batch-clock");
    rsx_capture_schedule s; uint32_t index = 99; unsigned reason;
    rsx_capture_schedule_init(&s, 200, 3, 7);
    check(!rsx_capture_schedule_next(&s, 199, 1, 1, &index), "old start");
    check(rsx_capture_schedule_next(&s, 200, 1, 0, &index) == 1 && index == 0, "old offscreen schedule retained");
    check(rsx_capture_schedule_next(&s, 207, 1, 1, &index) == 1 && index == 1, "old stride");
    check(!rsx_capture_schedule_presented(&s, 121, 1) && !s.presented_mode, "bounded late budget");
    check(!rsx_capture_schedule_presented(&s, 1, 0) && !s.presented_mode, "nonzero stride");
    check(!rsx_capture_schedule_presented(&s, 1, 10001) && !s.presented_mode, "bounded stride");
    check(rsx_capture_schedule_arm(&s, 2), "burst armed before phase");
    check(rsx_capture_schedule_presented(&s, 3, 2) && s.index == 2 && s.burst_remaining == 2, "phase preserves index and burst");
    check(!rsx_capture_schedule_presented(&s, 3, 2), "one late phase");
    check(!rsx_capture_schedule_next(&s, 999999, 1, 0, &index) && !s.presented_frames, "offscreen consumes neither clock nor budget");
    check(!rsx_capture_schedule_next(&s, 999999, 0, 1, &index) && !s.presented_frames, "empty present consumes neither clock nor budget");
    reason = rsx_capture_schedule_next(&s, 999999, 1, 1, &index);
    check(reason == 3 && index == 2 && s.done == 1 && s.burst_remaining == 1, "overlap once");
    reason = rsx_capture_schedule_next(&s, 1000000, 1, 1, &index);
    check(reason == 2 && index == 3 && s.done == 1 && !s.burst_remaining, "burst independent of cadence");
    check(rsx_capture_schedule_next(&s, 1000001, 1, 1, &index) == 1 && index == 4, "third drawn present");
    check(!rsx_capture_schedule_next(&s, 1000002, 1, 1, &index), "fourth drawn present skipped");
    check(rsx_capture_schedule_next(&s, 1000003, 1, 1, &index) == 1 && index == 5, "fifth drawn present");
    check(!rsx_capture_schedule_next(&s, 1000004, 1, 1, &index) && s.index == 6, "late budget exhausted");
    check(!rsx_capture_schedule_arm(&s, 2), "burst remains one shot");
    rsx_capture_schedule_init(&s, 200, 3, 7);
    rsx_capture_schedule_presented(&s, 2, 5);
    if (legacy) s.presented_mode = 0;
    unsigned captured = 0, offscreen = 0; unsigned ordinals[2] = {0,0};
    for (unsigned i = 0; i < 24; ++i) {
        int presenting = (i % 3) == 2;
        if (rsx_capture_schedule_next(&s, 1000 + i, 1, presenting, &index)) {
            if (!presenting) ++offscreen;
            if (captured < 2) ordinals[captured] = i / 3 + 1;
            ++captured;
        }
    }
    check(captured == 2 && !offscreen && ordinals[0] == 1 && ordinals[1] == 6, "present cadence independent of batch identifiers");
    printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

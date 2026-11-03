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
    rsx_capture_schedule_init(&s, 2, 3, 2);
    check(!s.screen_only, "raw diagnostic mode remains default");
    check(rsx_capture_schedule_screen_only(&s, "1") && s.screen_only, "screen mode enabled");
    check(!rsx_capture_schedule_screen_only(&s, "") && s.screen_only, "empty mode rejected without mutation");
    check(!rsx_capture_schedule_screen_only(&s, "2") && s.screen_only, "invalid mode rejected without mutation");
    check(rsx_capture_schedule_screen_only(&s, "0") && !s.screen_only, "explicit raw mode");
    check(rsx_capture_schedule_screen_only(&s, NULL) && !s.screen_only, "unset raw mode");
    rsx_capture_schedule_screen_only(&s, "1");
    if (argc > 1 && !strcmp(argv[1], "--ignore-screen")) s.screen_only = 0;
    captured = offscreen = 0;
    unsigned batches[3] = {0,0,0};
    for (unsigned frame = 0; frame < 9; ++frame) {
        int presenting = (frame & 1) != 0;
        if (rsx_capture_schedule_next(&s, frame, 1, presenting, &index)) {
            if (!presenting) ++offscreen;
            if (captured < 3) batches[captured] = frame;
            ++captured;
        }
    }
    check(captured == 3 && !offscreen && batches[0] == 3 && batches[1] == 5 &&
        batches[2] == 7 && s.index == 3 && s.done == 3,
        "periodic offscreen alias defers without spending screen budget");
    rsx_capture_schedule_init(&s, 1, 1, 4);
    rsx_capture_schedule_screen_only(&s, "1");
    check(rsx_capture_schedule_arm(&s, 2), "screen burst armed");
    check(!rsx_capture_schedule_next(&s, 1, 0, 1, &index) &&
        !s.done && !s.index && s.burst_remaining == 2, "empty screen consumes nothing");
    check(!rsx_capture_schedule_next(&s, 2, 1, 0, &index) &&
        !s.done && !s.index && s.burst_remaining == 2, "offscreen consumes neither budget");
    check(rsx_capture_schedule_next(&s, 3, 1, 1, &index) == 3 &&
        !index && s.done == 1 && s.burst_remaining == 1, "deferred sample overlaps burst once");
    check(!rsx_capture_schedule_next(&s, 4, 1, 0, &index) &&
        s.burst_remaining == 1 && s.index == 1, "offscreen preserves remaining burst");
    check(rsx_capture_schedule_next(&s, 5, 1, 1, &index) == 2 &&
        index == 1 && s.done == 1 && !s.burst_remaining, "remaining burst only on screen");
    check(!rsx_capture_schedule_next(&s, 6, 1, 1, &index) &&
        s.index == 2, "screen and burst bounds preserved");
    printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

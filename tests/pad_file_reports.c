/* Exercise the production API with authored time, no physical input, and guest
 * memory. */
#include "../runtime/platform/win32_compat.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <xinput.h>
static ULONGLONG authored_clock(void) { return 1000; }
static DWORD WINAPI authored_xinput(DWORD port, XINPUT_STATE *state) {
  (void)port;
  memset(state, 0, sizeof(*state));
  return ERROR_DEVICE_NOT_CONNECTED;
}
static SHORT WINAPI authored_key(int key) {
  (void)key;
  return 0;
}
#define GetTickCount64 authored_clock
#define XInputGetState authored_xinput
#define GetAsyncKeyState authored_key
#ifndef PAD_IMPLEMENTATION
#define PAD_IMPLEMENTATION "../libs/input/cellPad.c"
#endif
#include PAD_IMPLEMENTATION
static uint8_t memory[4096];
uint8_t *vm_base = memory;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t a) { (void)a; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) {
  (void)a;
  (void)v;
  (void)w;
}
int spu_coh_is_reserved(uint32_t a) {
  (void)a;
  return 0;
}
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
void spu_coh_notify_write(uint32_t a) { (void)a; }
static unsigned checks, failures;
static void check(int ok, const char *label) {
  checks++;
  if (!ok) {
    failures++;
    printf("FAIL %s\n", label);
  }
}
static unsigned read16(unsigned a) {
  return ((unsigned)memory[a] << 8) | memory[a + 1];
}
static unsigned read32(unsigned a) { return (read16(a) << 16) | read16(a + 2); }
static void call(unsigned port) {
  if (cellPadGetData(port, (CellPadData *)(uintptr_t)256) != CELL_OK) {
    fprintf(stderr, "GetData failed\n");
    exit(2);
  }
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  _putenv_s("PAD_FILE", argv[1]);
  const char *knobs[] = {"YDKJ_INJECT_PAD", "PAD_AUTOPRESS",
                         "PAD_SCRIPT",      "PAD_STICK",
                         "PAD_SWEEP",       "PAD_ALWAYS_CHANGE"};
  for (unsigned i = 0; i < sizeof(knobs) / sizeof(knobs[0]); i++)
    _putenv_s(knobs[i], "");
  s_pad_initialized = 1;
  s_max_connect = 2;
  pad_poll_backend();
  s_port_setting[0] = CELL_PAD_SETTING_PRESS_ON;
  FILE *f = fopen(argv[1], "wb");
  if (!f)
    return 2;
  fputs("0x4000 4\n", f);
  fclose(f);
  unsigned delivered = 0, pressed = 0, drain_reports = 0, pressure_ok = 1;
  for (unsigned n = 0; n < 4; n++) {
    s_data_fresh[0] = 1;
    call(0);
    if (read32(256)) {
      delivered++;
      unsigned down = read16(260 + CELL_PAD_BTN_OFFSET_DIGITAL2 * 2) & 0x40;
      if (down)
        pressed++;
      if (down && read16(260 + CELL_PAD_BTN_OFFSET_PRESS_CROSS * 2) != 255)
        pressure_ok = 0;
    }
    for (unsigned j = 0; j < 20; j++) {
      s_data_fresh[0] = 0;
      call(0);
      if (read32(256))
        drain_reports++;
    }
    s_data_fresh[1] = 1;
    call(1);
  }
  check(delivered == 4, "four actual reports");
  check(pressed == 4, "hold survives empty drain reads");
  check(drain_reports == 0, "drain still terminates");
  check(pressure_ok, "synthetic pressure preserved");
  s_data_fresh[0] = 1;
  call(0);
  check(!(read16(260 + CELL_PAD_BTN_OFFSET_DIGITAL2 * 2) & 0x40),
        "pulse releases after four reports");
  f = fopen(argv[1], "rb");
  int ack = f && fgetc(f) == EOF;
  if (f)
    fclose(f);
  check(ack, "command acknowledged");
  remove(argv[1]);
  printf("delivered=%u pressed=%u checks=%u failures=%u\n", delivered, pressed,
         checks, failures);
  return failures ? 1 : 0;
}

/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Host test of the logic of src/display_settings.c, with a fake clock. One scenario per
 * process: the module keeps its state in statics. Usage: test <scenario>; without an
 * argument it lists the scenarios. Built and run by tests/unit/test_display_settings.py.
 */
#include <errno.h>

#include "stub_all.h"

#include "display_settings.h"

void sim_advance(int64_t ms);
int64_t sim_now(void);
extern int g_screen_on, g_contrast, g_blank_on_calls, g_blank_off_calls, g_contrast_calls;
extern int g_save_calls, g_delete_calls, g_kicks;
struct record {
  char key[48];
  int32_t value;
  size_t len;
  bool used;
};
extern struct record g_store[32];
extern int g_queue_runs_display;

static int g_fail;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("  FAIL line %d: %s\n", __LINE__, #cond);                         \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

#define SEC(n) ((int64_t)(n) * 1000)

/* ---- helpers --------------------------------------------------------------- */
struct fake_rec {
  const char *name;
  int32_t value;
  size_t len;
};

static const struct fake_rec *g_cur_rec;

static ssize_t fake_read(void *cb_arg, void *data, size_t len) {
  (void)cb_arg;
  size_t n = len < g_cur_rec->len ? len : g_cur_rec->len;
  memcpy(data, &g_cur_rec->value, n);
  return (ssize_t)n;
}

static void boot_with(const struct fake_rec *recs, size_t count) {
  g_sys_init_fn();
  for (size_t i = 0; i < count; i++) {
    g_cur_rec = &recs[i];
    g_settings_handler.h_set(recs[i].name, recs[i].len, fake_read, NULL);
  }
  g_settings_handler.h_commit();
  sim_advance(SEC(5));
}

static void boot(void) { boot_with(NULL, 0); }

static void event(void) { g_listener_cb(NULL); }

static void go_idle(void) {
  g_activity = ZMK_ACTIVITY_IDLE;
  event();
}

static void go_active(void) {
  g_activity = ZMK_ACTIVITY_ACTIVE;
  event();
}

static int store_count(void) {
  int n = 0;
  for (size_t i = 0; i < 32; i++) {
    n += g_store[i].used;
  }
  return n;
}

static struct record *store_find(const char *key) {
  for (size_t i = 0; i < 32; i++) {
    if (g_store[i].used && strcmp(g_store[i].key, key) == 0) {
      return &g_store[i];
    }
  }
  return NULL;
}

/* ---- scenarios ------------------------------------------------------------- */
static void s_defaults(void) {
  boot();
  CHECK(g_contrast == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_LEFT_CONTRAST) == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_RIGHT_CONTRAST) == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_MODE) == 0);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S) == 60);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST) == 1);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST_MS) == 1500);
  CHECK(kp_display_setting_count() == KP_DISPLAY_SETTING_COUNT);

  struct kp_display_setting_info info;
  CHECK(kp_display_setting_info(3, &info) == 0);
  CHECK(info.id == 4 && info.min == 30 && info.max == 3600 && info.default_value == 60 &&
        info.value == 60);
  CHECK(kp_display_setting_info(0, &info) == 0);
  CHECK(info.id == 1 && info.min == 0 && info.max == 255 && info.default_value == 128);
  CHECK(kp_display_setting_info(6, &info) == -EINVAL);
  CHECK(g_screen_on == 1 && g_blank_on_calls == 0);
  CHECK(g_store[0].used == false);
}

static void s_contrast_persist(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 200) == 0);
  CHECK(g_contrast == 200);
  CHECK(g_kicks >= 1);
  sim_advance(2900);
  CHECK(store_find("keypaw/display/1") == NULL);
  sim_advance(200);
  struct record *r = store_find("keypaw/display/1");
  CHECK(r != NULL && r->value == 200 && r->len == 4);
  /* back to the default: the record goes */
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 128) == 0);
  sim_advance(SEC(3) + 100);
  CHECK(store_find("keypaw/display/1") == NULL);
  /* the same value again: nothing happens */
  int kicks = g_kicks;
  int calls = g_contrast_calls;
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 128) == 0);
  CHECK(g_kicks == kicks && g_contrast_calls == calls);
  CHECK(g_log_warnings == 0);
}

static void s_debounce_batch(void) {
  boot();
  for (int i = 0; i < 10; i++) {
    CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 1000 + i * 100) == 0);
    sim_advance(100);
  }
  CHECK(g_save_calls == 0);
  sim_advance(SEC(3));
  CHECK(g_save_calls == 1);
  struct record *r = store_find("keypaw/display/6");
  CHECK(r != NULL && r->value == 1900);
}

static void s_range(void) {
  boot();
  CHECK(kp_display_setting_set(4, 10) == -EINVAL);
  CHECK(kp_display_setting_set(9, 1) == -ENOENT);
  CHECK(kp_display_setting_set(0, 1) == -ENOENT);
  CHECK(kp_display_setting_set(1, 256) == -EINVAL);
  CHECK(kp_display_setting_set(1, -1) == -EINVAL);
  CHECK(kp_display_setting_set(3, 3) == -EINVAL);
  CHECK(kp_display_setting_set(5, 2) == -EINVAL);
  CHECK(kp_display_setting_set(6, 499) == -EINVAL);
  CHECK(kp_display_setting_set(6, 5001) == -EINVAL);
  CHECK(kp_display_setting_set(4, 30) == 0);
  CHECK(kp_display_setting_set(4, 3600) == 0);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S) == 3600);
  /* a rejected value changes nothing */
  CHECK(kp_display_setting_set(6, 99999) == -EINVAL);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST_MS) == 1500);
}

static void s_never(void) {
  boot();
  go_idle();
  sim_advance(SEC(120));
  CHECK(g_screen_on == 1 && g_blank_on_calls == 0);
  go_active();
  CHECK(g_screen_on == 1);
}

static void s_always(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_AFTER_S, 60) == 0);
  sim_advance(SEC(15));
  CHECK(g_screen_on == 1); /* active: stays on */
  CHECK(g_blank_on_calls == 0);
  go_idle();
  sim_advance(SEC(29));
  CHECK(g_screen_on == 1);
  sim_advance(1100);
  CHECK(g_screen_on == 0 && g_blank_on_calls == 1);
  go_active();
  CHECK(g_screen_on == 1);
  /* activity again, then idle again: off again after the same time */
  go_idle();
  sim_advance(SEC(30) + 100);
  CHECK(g_screen_on == 0);
  CHECK(g_log_warnings == 0);
}

static void s_always_min(void) {
  /* after = the idle time of ZMK: the screen goes off at the idle event */
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_AFTER_S, 30) == 0);
  sim_advance(SEC(11));
  go_idle();
  sim_advance(10);
  CHECK(g_screen_on == 0);
}

static void s_battery(void) {
  boot();
  g_usb_powered = 1;
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 1) == 0);
  go_idle();
  sim_advance(SEC(100));
  CHECK(g_screen_on == 1 && g_blank_on_calls == 0);
  g_usb_powered = 0;
  event(); /* the USB event */
  sim_advance(SEC(29));
  CHECK(g_screen_on == 1);
  sim_advance(SEC(2));
  CHECK(g_screen_on == 0);
  g_usb_powered = 1;
  event();
  CHECK(g_screen_on == 1);
  sim_advance(SEC(100));
  CHECK(g_screen_on == 1);
}

static void s_wake_while_blanked(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  go_idle();
  sim_advance(SEC(31));
  CHECK(g_screen_on == 0);
  /* a contrast change wakes the screen and it goes off again 30 s later (60 - idle 30) */
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 100) == 0);
  CHECK(g_screen_on == 1 && g_contrast == 100);
  sim_advance(SEC(29) + 900);
  CHECK(g_screen_on == 1);
  sim_advance(200);
  CHECK(g_screen_on == 0);
  /* with after = idle, the wake still lasts 10 s */
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_AFTER_S, 30) == 0);
  CHECK(g_screen_on == 1);
  sim_advance(SEC(9) + 900);
  CHECK(g_screen_on == 1);
  sim_advance(200);
  CHECK(g_screen_on == 0);
}

static void s_back_to_never(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  go_idle();
  sim_advance(SEC(40));
  CHECK(g_screen_on == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 0) == 0);
  CHECK(g_screen_on == 1);
  sim_advance(SEC(300));
  CHECK(g_screen_on == 1);
}

static void s_mode_change_while_idle(void) {
  /* idle for a long time with mode never; then the mode changes to always */
  boot();
  go_idle();
  sim_advance(SEC(200));
  CHECK(g_screen_on == 1);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  CHECK(g_screen_on == 1);
  /* delay is max(60 - 30, 10) = 30 s */
  sim_advance(SEC(29));
  CHECK(g_screen_on == 1);
  sim_advance(SEC(2));
  CHECK(g_screen_on == 0);
}

static void s_load(void) {
  static const struct fake_rec recs[] = {
      {"1", 300, 4},   /* out of range: ignored */
      {"3", 2, 4},     /* valid */
      {"15", 5, 4},    /* unknown id: ignored */
      {"4", 70, 2},    /* wrong length: ignored */
      {"x", 1, 4},     /* not a number: ignored */
      {"6", 2000, 4},  /* valid */
      {"2", 55, 4},    /* valid */
      {"5x", 0, 4},    /* garbage after the number: ignored */
      {"-3", 1, 4},    /* negative: ignored */
  };
  boot_with(recs, sizeof(recs) / sizeof(recs[0]));
  CHECK(kp_display_setting_get(KP_DISPLAY_LEFT_CONTRAST) == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_RIGHT_CONTRAST) == 55);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_MODE) == 2);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S) == 60);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST) == 1);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST_MS) == 2000);
  CHECK(g_log_warnings == 0);
}

static void s_boot_apply(void) {
  static const struct fake_rec recs[] = {{"1", 60, 4}};
  boot_with(recs, 1);
  CHECK(g_contrast == 60);
}

static void s_restore(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 10) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 2) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST, 0) == 0);
  sim_advance(SEC(4));
  CHECK(store_count() == 3);
  int kicks = g_kicks;
  CHECK(kp_display_settings_restore_defaults() == 0);
  CHECK(g_contrast == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_MODE) == 0);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST) == 1);
  CHECK(g_kicks == kicks + 3);
  sim_advance(SEC(4));
  CHECK(store_count() == 0);
}

static void s_reset(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 200) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 3000) == 0);
  sim_advance(SEC(4));
  CHECK(store_count() == 2);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 4000) == 0); /* still waiting to be written */
  CHECK(kp_display_settings_settings_reset() == 0);
  CHECK(store_count() == 0); /* gone at once, not after the debounce */
  CHECK(kp_display_setting_get(KP_DISPLAY_LEFT_CONTRAST) == 128);
  CHECK(kp_display_setting_get(KP_DISPLAY_TOAST_MS) == 1500);
  sim_advance(SEC(10));
  CHECK(store_count() == 0); /* and nothing writes them back */
}

static void s_sync(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 2500) == 0);
  uint32_t p1, p2;
  for (size_t i = 0; i < KP_DISPLAY_SETTING_COUNT; i++) {
    CHECK(kp_display_sync_get(i, &p1, &p2));
    CHECK(RGB_DISPLAY_IS_CMD(p1));
    CHECK(RGB_DISPLAY_PARAM1_ID(p1) == i + 1);
    CHECK(RGB_DISPLAY_PARAM1_CMD(p1) == RGB_DISPLAY_SET_CMD);
  }
  CHECK(kp_display_sync_get(5, &p1, &p2) && p1 == 0x6106 && p2 == 2500);
  CHECK(!kp_display_sync_get(KP_DISPLAY_SETTING_COUNT, &p1, &p2));
}

static void s_command(void) {
  boot();
  uint32_t p1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, 4);
  CHECK(RGB_DISPLAY_IS_CMD(p1));
  CHECK(!RGB_DISPLAY_IS_CMD(0x104));
  CHECK(!RGB_DISPLAY_IS_CMD(0x105));
  CHECK(!RGB_DISPLAY_IS_CMD(0x10106));
  CHECK(!RGB_DISPLAY_IS_CMD(0x100));
  CHECK(kp_display_settings_command(p1, 90) == 0);
  CHECK(kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S) == 90);
  CHECK(kp_display_settings_command(RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, 9), 1) == -ENOENT);
  CHECK(kp_display_settings_command(p1, 5) == -EINVAL);
  /* a negative int32 carried as uint32 */
  CHECK(kp_display_settings_command(RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, 1), (uint32_t)-5) ==
        -EINVAL);
}

static void s_peripheral(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_RIGHT_CONTRAST, 50) == 0);
  CHECK(g_contrast == 50);
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 200) == 0);
  CHECK(g_contrast == 50); /* the right half applies the right value */
  CHECK(kp_display_setting_get(KP_DISPLAY_LEFT_CONTRAST) == 200);
}

static void s_peripheral_boot(void) {
  static const struct fake_rec recs[] = {{"1", 60, 4}, {"2", 70, 4}};
  boot_with(recs, 2);
  CHECK(g_contrast == 70);
}

static void s_central_applies_left(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_RIGHT_CONTRAST, 50) == 0);
  CHECK(g_contrast == 128); /* the central keeps its own left value */
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 90) == 0);
  CHECK(g_contrast == 90);
}

struct scenario {
  const char *name;
  void (*fn)(void);
};

static const struct scenario scenarios[] = {
    {"defaults", s_defaults},
    {"contrast_persist", s_contrast_persist},
    {"debounce_batch", s_debounce_batch},
    {"range", s_range},
    {"never", s_never},
    {"always", s_always},
    {"always_min", s_always_min},
    {"battery", s_battery},
    {"wake_while_blanked", s_wake_while_blanked},
    {"back_to_never", s_back_to_never},
    {"mode_change_while_idle", s_mode_change_while_idle},
    {"load", s_load},
    {"boot_apply", s_boot_apply},
    {"restore", s_restore},
    {"reset", s_reset},
    {"sync", s_sync},
    {"command", s_command},
    {"central_applies_left", s_central_applies_left},
    {"peripheral", s_peripheral},
    {"peripheral_boot", s_peripheral_boot},
};

int main(int argc, char **argv) {
  if (argc < 2) {
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
      printf("%s\n", scenarios[i].name);
    }
    return 0;
  }
  for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
    if (strcmp(scenarios[i].name, argv[1]) == 0) {
      scenarios[i].fn();
      printf("%s: %s (t=%lld ms)\n", scenarios[i].name, g_fail ? "FAIL" : "ok",
             (long long)sim_now());
      return g_fail;
    }
  }
  printf("unknown scenario %s\n", argv[1]);
  return 99;
}

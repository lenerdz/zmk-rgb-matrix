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
extern struct record g_store[64];
extern int g_queue_runs_display;
extern int g_layout_events, g_layout_last_id;

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
  for (size_t i = 0; i < 64; i++) {
    n += g_store[i].used;
  }
  return n;
}

static struct record *store_find(const char *key) {
  for (size_t i = 0; i < 64; i++) {
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
#ifndef CONFIG_KEYPAW_OLED_LAYOUT
  CHECK(kp_display_setting_count() == 6);
#endif

  struct kp_display_setting_info info;
  CHECK(kp_display_setting_info(3, &info) == 0);
  CHECK(info.id == 4 && info.min == 30 && info.max == 3600 && info.default_value == 60 &&
        info.value == 60);
  CHECK(kp_display_setting_info(0, &info) == 0);
  CHECK(info.id == 1 && info.min == 0 && info.max == 255 && info.default_value == 128);
#ifndef CONFIG_KEYPAW_OLED_LAYOUT
  CHECK(kp_display_setting_info(6, &info) == -EINVAL);
#endif
  CHECK(kp_display_setting_info(kp_display_setting_count(), &info) == -EINVAL);
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
      {"256", 1, 4},   /* does not fit the 8 bits of an id: ignored */
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

/* The peripheral applies every setting but the left contrast; the central sends
 * only the ones that differ from what it remembers sending. */
static void s_sync(void) {
  boot();
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 2500) == 0);

  int32_t shadow[KP_DISPLAY_SETTING_CAP];
  for (size_t i = 0; i < KP_DISPLAY_SETTING_CAP; i++) {
    shadow[i] = KP_DISPLAY_SYNC_UNKNOWN;
  }

  static const uint8_t expect[] = {2, 3, 4, 5, 6};
  uint32_t p1, p2;
  size_t sent = 0;
  int next = 0;

  while ((next = kp_display_sync_next((size_t)next, shadow)) >= 0) {
    CHECK(kp_display_sync_command((size_t)next, &p1, &p2));
    CHECK(RGB_DISPLAY_IS_CMD(p1));
    CHECK(RGB_DISPLAY_PARAM1_CMD(p1) == RGB_DISPLAY_SET_CMD);
    if (sent < sizeof(expect)) {
      CHECK(RGB_DISPLAY_PARAM1_ID(p1) == expect[sent]);
    }
    shadow[next] = (int32_t)p2;
    sent++;
    next++;
  }
  CHECK(sent == sizeof(expect));
  CHECK(kp_display_sync_next(0, shadow) == -1);

  /* the left contrast is the central's own: it is never sent */
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 90) == 0);
  CHECK(kp_display_sync_next(0, shadow) == -1);

  /* a change: only that setting, with its value */
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 3000) == 0);
  next = kp_display_sync_next(0, shadow);
  CHECK(next == 5 && kp_display_sync_command((size_t)next, &p1, &p2) &&
        p1 == 0x6106 && p2 == 3000);
  shadow[next] = (int32_t)p2;
  CHECK(kp_display_sync_next(0, shadow) == -1);

  /* the search starts where it is told to, and a connection that has just
   * been made knows nothing */
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 1) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 2000) == 0);
  CHECK(kp_display_sync_next(0, shadow) == 2);
  CHECK(kp_display_sync_next(3, shadow) == 5);
  CHECK(kp_display_sync_next(6, shadow) == -1);
  CHECK(!kp_display_sync_command(kp_display_setting_count(), &p1, &p2));
}

static void s_command(void) {
  boot();
  uint32_t p1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, 4);
  CHECK(RGB_DISPLAY_IS_CMD(p1));
  CHECK(!RGB_DISPLAY_IS_CMD(0x104));
  CHECK(!RGB_DISPLAY_IS_CMD(0x105));
  /* the id has 8 bits: 16 and up are display commands too, and 256 is not one */
  CHECK(RGB_DISPLAY_IS_CMD(0x10106));
  CHECK(RGB_DISPLAY_IS_CMD(0xFF106));
  CHECK(!RGB_DISPLAY_IS_CMD(0x100106));
  CHECK(!RGB_DISPLAY_IS_CMD(0x11105));
  CHECK(RGB_DISPLAY_PARAM1_ID(0xFF106) == 255);
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

#ifdef CONFIG_KEYPAW_OLED_LAYOUT

/* The OLED layout settings (ids 16 and up). The test is built with the options of
 * zmk-nice-oled that a Corne with 128x32 screens has: see
 * tests/unit/test_display_settings.py. */

static void check_layout_default(int line, enum kp_display_setting_id id,
                                 int32_t expected) {
  if (kp_display_setting_get(id) != expected) {
    printf("  FAIL line %d: setting %d is %d, not %d\n", line, (int)id,
           (int)kp_display_setting_get(id), (int)expected);
    g_fail++;
  }
}

#define DEFAULT_IS(id, value) check_layout_default(__LINE__, id, value)

static void s_layout_defaults(void) {
  boot();

  /* the right screen, as the build chooses it */
  DEFAULT_IS(KP_OLED_R_ANIM_SHOW, 1);
  DEFAULT_IS(KP_OLED_R_ANIM_X, 18);
  DEFAULT_IS(KP_OLED_R_ANIM_Y, -18);
  DEFAULT_IS(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_CAT);
  DEFAULT_IS(KP_OLED_R_ANIM_MS, 0);
  /* not turned, flipping what it is over, the whole picture */
  DEFAULT_IS(KP_OLED_R_ANIM_ROT, 0);
  DEFAULT_IS(KP_OLED_R_ANIM_MIX, KP_OLED_MIX_INVERT);
  DEFAULT_IS(KP_OLED_R_ANIM_CROP_XY, 0);
  DEFAULT_IS(KP_OLED_R_ANIM_CROP_WH, 0);
  DEFAULT_IS(KP_OLED_R_BATTERY_SHOW, 1);
  DEFAULT_IS(KP_OLED_R_BATTERY_X, 0);
  DEFAULT_IS(KP_OLED_R_BATTERY_Y, 50);
  DEFAULT_IS(KP_OLED_R_LINK_SHOW, 1);
  DEFAULT_IS(KP_OLED_R_LINK_X, 4);
  DEFAULT_IS(KP_OLED_R_LINK_Y, 32);
  DEFAULT_IS(KP_OLED_R_TOAST_X, 0);
  DEFAULT_IS(KP_OLED_R_TOAST_Y, 143);

#ifdef TEST_LEFT_WIDGETS
  /* the left screen */
  CHECK(kp_display_setting_count() == 47);
  DEFAULT_IS(KP_OLED_L_OUTPUT_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_OUTPUT_X, 0);
  DEFAULT_IS(KP_OLED_L_OUTPUT_Y, 34);
  DEFAULT_IS(KP_OLED_L_BATTERY_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_BATTERY_X, 0);
  DEFAULT_IS(KP_OLED_L_BATTERY_Y, 50);
  DEFAULT_IS(KP_OLED_L_PROFILE_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_PROFILE_X, 0);
  DEFAULT_IS(KP_OLED_L_PROFILE_Y, 137);
  DEFAULT_IS(KP_OLED_L_PROFILE_NUM_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_PROFILE_NUM_X, 25);
  DEFAULT_IS(KP_OLED_L_PROFILE_NUM_Y, 32);
  DEFAULT_IS(KP_OLED_L_LAYER_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_LAYER_X, 0);
  DEFAULT_IS(KP_OLED_L_LAYER_Y, 146);
  DEFAULT_IS(KP_OLED_L_WPM_GAUGE_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_WPM_GAUGE_X, 0);
  DEFAULT_IS(KP_OLED_L_WPM_GAUGE_Y, 70);
  DEFAULT_IS(KP_OLED_L_WPM_NUMBER_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_WPM_NUMBER_X, 0);
  DEFAULT_IS(KP_OLED_L_WPM_NUMBER_Y, 75);
  DEFAULT_IS(KP_OLED_L_WPM_GRAPH_SHOW, 1);
  DEFAULT_IS(KP_OLED_L_WPM_GRAPH_X, -1);
  DEFAULT_IS(KP_OLED_L_WPM_GRAPH_Y, 95);
#endif

  /* the list ZMK Studio gets: ranges by kind of setting */
  int shown = 0;
  for (size_t i = 0; i < kp_display_setting_count(); i++) {
    struct kp_display_setting_info info;
    CHECK(kp_display_setting_info(i, &info) == 0);
    if (info.id < KP_OLED_R_ANIM_SHOW) {
      continue;
    }
    shown++;
    CHECK(info.value == info.default_value);
    CHECK(info.default_value >= info.min && info.default_value <= info.max);
    if (info.id == KP_OLED_R_ANIM_KIND) {
      CHECK(info.min == 0 && info.max == KP_OLED_ANIM_KIND_COUNT - 1);
    } else if (info.id == KP_OLED_R_ANIM_MS) {
      CHECK(info.min == 0 && info.max == 60000);
    } else if (info.id == KP_OLED_R_ANIM_ROT) {
      CHECK(info.min == 0 && info.max == 3);
    } else if (info.id == KP_OLED_R_ANIM_MIX) {
      CHECK(info.min == KP_OLED_MIX_ADD && info.max == KP_OLED_MIX_INVERT);
    } else if (info.id == KP_OLED_R_ANIM_CROP_XY ||
               info.id == KP_OLED_R_ANIM_CROP_WH) {
      CHECK(info.min == 0 && info.max == 65535);
    } else if (info.id % 8 == 0) {
      CHECK(info.min == 0 && info.max == 1); /* a switch */
    } else {
      CHECK(info.min == KP_OLED_COORD_MIN && info.max == KP_OLED_COORD_MAX);
    }
  }
#ifdef TEST_LEFT_WIDGETS
  CHECK(shown == 41);
#else
  CHECK(shown == 17);
#endif

  /* kp_display_setting_default is what restoring sets, whatever the value is now */
  int32_t kind_default = kp_display_setting_get(KP_OLED_R_ANIM_KIND);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_VIM) == 0);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_KIND) == KP_OLED_ANIM_VIM);
  CHECK(kp_display_setting_default(KP_OLED_R_ANIM_KIND) == kind_default);
  CHECK(kp_display_setting_default(KP_OLED_R_ANIM_X) == 18);
  CHECK(kp_display_setting_default((enum kp_display_setting_id)7) == 0);
  CHECK(g_log_warnings == 0);
}

static void s_layout_range(void) {
  boot();
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, 321) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, -161) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, 320) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, -160) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_SHOW, 2) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_SHOW, -1) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_KIND_COUNT) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_VIP_MARCOS) == 0);
  /* the pictures that come after them: the first and the last of each batch */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_EQUALIZER) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_ROCKET) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_WATERFALL) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_MUSHROOM) == 0);
  CHECK(KP_OLED_ANIM_ROCKET + 1 == KP_OLED_ANIM_WATERFALL);
  CHECK(KP_OLED_ANIM_MUSHROOM == KP_OLED_ANIM_KIND_COUNT - 1);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_KIND_COUNT) == -EINVAL);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_KIND) == KP_OLED_ANIM_MUSHROOM);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MS, 60001) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MS, 60000) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MS, -1) == -EINVAL);
  /* ids that no widget has: the gaps of the blocks, the toast switch that is
   * setting 5, and what is past the last one */
  CHECK(kp_display_setting_set(7, 1) == -ENOENT);
  CHECK(kp_display_setting_set(21, 1) == -ENOENT);
  CHECK(kp_display_setting_set(40, 1) == -ENOENT);
  CHECK(kp_display_setting_set(107, 1) == -ENOENT);
  CHECK(kp_display_setting_set(111, 1) == -ENOENT);
  CHECK(kp_display_setting_set(116, 1) == -ENOENT);
  CHECK(kp_display_setting_set(255, 1) == -ENOENT);
  /* a rejected value changes nothing */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_Y, 999) == -EINVAL);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_Y) == -18);
}

/* The picture of the right screen: how it is turned, how it mixes and which piece of it shows. */
static void s_layout_picture(void) {
  boot();
  int events = g_layout_events;

  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_ROT, 4) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_ROT, -1) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_ROT, 3) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MIX, 2) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MIX, KP_OLED_MIX_ADD) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_XY, 65536) == -EINVAL);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_XY, -1) == -EINVAL);
  /* x 5, y 10: 5 | 10 << 8 */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_XY, 5 | (10 << 8)) == 0);
  /* 32 wide, 69 tall */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_WH, 32 | (69 << 8)) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_WH, 65535) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_CROP_WH, 32 | (69 << 8)) == 0);

  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_ROT) == 3);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_MIX) == KP_OLED_MIX_ADD);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_CROP_XY) == 0x0a05);
  CHECK(kp_display_setting_get(KP_OLED_R_ANIM_CROP_WH) == 0x4520);

  /* the widgets draw again, once, for all of them */
  sim_advance(30);
  CHECK(g_layout_events == events + 1 && g_layout_last_id == KP_OLED_R_ANIM_CROP_WH);

  /* what differs from the default is kept in the flash, and the rest is not */
  sim_advance(SEC(4));
  struct record *r = store_find("keypaw/display/112");
  CHECK(r != NULL && r->value == 3 && r->len == 4);
  r = store_find("keypaw/display/113");
  CHECK(r != NULL && r->value == KP_OLED_MIX_ADD);
  r = store_find("keypaw/display/114");
  CHECK(r != NULL && r->value == 0x0a05);
  r = store_find("keypaw/display/115");
  CHECK(r != NULL && r->value == 0x4520);
  CHECK(store_count() == 4);

  /* the default of the mix is the one that is not stored */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_MIX, KP_OLED_MIX_INVERT) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_ROT, 0) == 0);
  sim_advance(SEC(4));
  CHECK(store_find("keypaw/display/112") == NULL);
  CHECK(store_find("keypaw/display/113") == NULL);
  CHECK(store_count() == 2);

  /* restoring the defaults puts the whole picture back */
  CHECK(kp_display_settings_restore_defaults() == 0);
  sim_advance(SEC(4));
  DEFAULT_IS(KP_OLED_R_ANIM_CROP_XY, 0);
  DEFAULT_IS(KP_OLED_R_ANIM_CROP_WH, 0);
  CHECK(store_count() == 0);
  CHECK(g_log_warnings == 0);
}

static void s_layout_event(void) {
  boot();
  /* once after the saved settings are loaded, whatever the widgets drew before */
  CHECK(g_layout_events == 1 && g_layout_last_id == 0);

  int events = g_layout_events;
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, 30) == 0);
  CHECK(g_layout_events == events); /* not from inside the call */
  sim_advance(19);
  CHECK(g_layout_events == events);
  sim_advance(2);
  CHECK(g_layout_events == events + 1 && g_layout_last_id == KP_OLED_R_ANIM_X);

  /* changes that follow each other make one event, with the last id */
  events = g_layout_events;
  CHECK(kp_display_setting_set(KP_OLED_R_BATTERY_X, 5) == 0);
  sim_advance(10);
  CHECK(kp_display_setting_set(KP_OLED_R_BATTERY_Y, 20) == 0);
  sim_advance(10);
  CHECK(g_layout_events == events);
  sim_advance(15);
  CHECK(g_layout_events == events + 1 && g_layout_last_id == KP_OLED_R_BATTERY_Y);

  /* the settings that are not part of the layout need no redraw */
  events = g_layout_events;
  CHECK(kp_display_setting_set(KP_DISPLAY_LEFT_CONTRAST, 90) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_RIGHT_CONTRAST, 90) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_OFF_MODE, 1) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST, 0) == 0);
  CHECK(kp_display_setting_set(KP_DISPLAY_TOAST_MS, 2000) == 0);
  sim_advance(100);
  CHECK(g_layout_events == events);

  /* the same value, or a rejected one: nothing */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, 30) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_X, 999) == -EINVAL);
  sim_advance(100);
  CHECK(g_layout_events == events);

  /* the defaults at once make one event */
  CHECK(kp_display_settings_restore_defaults() == 0);
  sim_advance(100);
  CHECK(g_layout_events == events + 1);
  DEFAULT_IS(KP_OLED_R_ANIM_X, 18);
  CHECK(g_log_warnings == 0);
}

static void s_layout_persist(void) {
  boot();
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_Y, -40) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_POKEMON) == 0);
  CHECK(kp_display_setting_set(KP_OLED_R_LINK_SHOW, 0) == 0);
#ifdef TEST_LEFT_WIDGETS
  /* the entries with index 32 and up have their dirty bit in the second word */
  CHECK(kp_display_setting_set(KP_OLED_L_WPM_GRAPH_SHOW, 0) == 0);
  CHECK(kp_display_setting_set(KP_OLED_L_WPM_GRAPH_Y, 100) == 0);
#endif
  sim_advance(SEC(2));
  CHECK(store_count() == 0); /* the debounce */
  sim_advance(SEC(2));

  struct record *r = store_find("keypaw/display/18");
  CHECK(r != NULL && r->value == -40 && r->len == 4);
  r = store_find("keypaw/display/19");
  CHECK(r != NULL && r->value == KP_OLED_ANIM_POKEMON);
  r = store_find("keypaw/display/32");
  CHECK(r != NULL && r->value == 0);
#ifdef TEST_LEFT_WIDGETS
  r = store_find("keypaw/display/104");
  CHECK(r != NULL && r->value == 0);
  r = store_find("keypaw/display/106");
  CHECK(r != NULL && r->value == 100);
  CHECK(store_count() == 5);
#else
  CHECK(store_count() == 3);
#endif

  /* back to the default: the record goes */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_Y, -18) == 0);
  sim_advance(SEC(4));
  CHECK(store_find("keypaw/display/18") == NULL);

  /* the settings reset takes them all at once */
  CHECK(kp_display_settings_settings_reset() == 0);
  CHECK(store_count() == 0);
  DEFAULT_IS(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_CAT);
  DEFAULT_IS(KP_OLED_R_LINK_SHOW, 1);
  sim_advance(SEC(10));
  CHECK(store_count() == 0);
}

static void s_layout_load(void) {
  static const struct fake_rec recs[] = {
      {"17", 30, 4},     /* valid */
      {"18", -40, 4},    /* valid, negative */
      {"19", KP_OLED_ANIM_KIND_COUNT, 4}, /* kind out of range: ignored */
      {"20", 99999, 4},  /* cycle time out of range: ignored */
      {"25", -161, 4},   /* coordinate out of range: ignored */
      {"32", 0, 4},      /* valid */
      {"41", 12, 2},     /* wrong length: ignored */
      {"107", 1, 4},     /* no such setting: ignored */
      {"7", 1, 4},       /* ditto */
      {"256", 1, 4},     /* does not fit 8 bits: ignored */
  };
  boot_with(recs, sizeof(recs) / sizeof(recs[0]));
  DEFAULT_IS(KP_OLED_R_ANIM_X, 30);
  DEFAULT_IS(KP_OLED_R_ANIM_Y, -40);
  DEFAULT_IS(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_CAT);
  DEFAULT_IS(KP_OLED_R_ANIM_MS, 0);
  DEFAULT_IS(KP_OLED_R_BATTERY_X, 0);
  DEFAULT_IS(KP_OLED_R_LINK_SHOW, 0);
  DEFAULT_IS(KP_OLED_R_TOAST_X, 0);
  CHECK(g_log_warnings == 0);
  /* what loaded is announced once, after the load */
  CHECK(g_layout_events == 1 && g_layout_last_id == 0);
}

/* What the central sends its peripheral: the settings the peripheral applies,
 * once each after a connection, then only the ones that change. */
static void s_layout_sync(void) {
  boot();

  int32_t shadow[KP_DISPLAY_SETTING_CAP];
  for (size_t i = 0; i < KP_DISPLAY_SETTING_CAP; i++) {
    shadow[i] = KP_DISPLAY_SYNC_UNKNOWN;
  }

  static const uint8_t expect[] = {2,  3,   4,   5,  6,  16, 17, 18,
                                   19, 20,  112, 113, 114, 115, 24, 25,
                                   26, 32,  33,  34,  41,  42};
  uint32_t p1, p2;
  size_t sent = 0;
  int next = 0;

  while ((next = kp_display_sync_next((size_t)next, shadow)) >= 0) {
    CHECK(kp_display_sync_command((size_t)next, &p1, &p2));
    CHECK(RGB_DISPLAY_IS_CMD(p1));
    if (sent < sizeof(expect)) {
      CHECK(RGB_DISPLAY_PARAM1_ID(p1) == expect[sent]);
    }
    shadow[next] = (int32_t)p2;
    sent++;
    next++;
  }
  CHECK(sent == sizeof(expect));
  CHECK(kp_display_sync_next(0, shadow) == -1);

#ifdef TEST_LEFT_WIDGETS
  /* the left screen belongs to the central: it is never sent */
  CHECK(kp_display_setting_set(KP_OLED_L_LAYER_Y, 100) == 0);
  CHECK(kp_display_setting_set(KP_OLED_L_WPM_GRAPH_SHOW, 0) == 0);
  CHECK(kp_display_sync_next(0, shadow) == -1);
#endif

  /* a change of the right screen: only that one, a negative value as it is */
  CHECK(kp_display_setting_set(KP_OLED_R_ANIM_Y, -30) == 0);
  next = kp_display_sync_next(0, shadow);
  CHECK(next >= 0 && kp_display_sync_command((size_t)next, &p1, &p2));
  CHECK(p1 == 0x12106 && p2 == (uint32_t)-30);
  shadow[next] = (int32_t)p2;
  CHECK(kp_display_sync_next(0, shadow) == -1);

  /* a connection that has just been made knows nothing: all of them again */
  for (size_t i = 0; i < KP_DISPLAY_SETTING_CAP; i++) {
    shadow[i] = KP_DISPLAY_SYNC_UNKNOWN;
  }
  sent = 0;
  next = 0;
  while ((next = kp_display_sync_next((size_t)next, shadow)) >= 0) {
    CHECK(kp_display_sync_command((size_t)next, &p1, &p2));
    shadow[next] = (int32_t)p2;
    sent++;
    next++;
  }
  CHECK(sent == sizeof(expect));
}

static void s_layout_command(void) {
  boot();
  uint32_t p1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, KP_OLED_R_ANIM_X);
  CHECK(p1 == 0x11106 && RGB_DISPLAY_IS_CMD(p1) && RGB_DISPLAY_PARAM1_ID(p1) == 17);
  CHECK(kp_display_settings_command(p1, (uint32_t)-25) == 0);
  DEFAULT_IS(KP_OLED_R_ANIM_X, -25);
  CHECK(kp_display_settings_command(p1, 400) == -EINVAL);
  CHECK(kp_display_settings_command(RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, 255), 1) ==
        -ENOENT);
  DEFAULT_IS(KP_OLED_R_ANIM_X, -25);
}

/* The peripheral keeps only the settings of its own screen. */
static void s_layout_peripheral(void) {
  boot();
  CHECK(kp_display_setting_count() == 23);
  DEFAULT_IS(KP_OLED_R_ANIM_X, 18);
  CHECK(kp_display_setting_set(KP_OLED_L_LAYER_X, 5) == -ENOENT);
  CHECK(kp_display_setting_set(KP_OLED_L_OUTPUT_SHOW, 0) == -ENOENT);
  CHECK(kp_display_setting_get(KP_OLED_L_LAYER_X) == 0);

  int events = g_layout_events;
  uint32_t p1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, KP_OLED_R_ANIM_Y);
  CHECK(kp_display_settings_command(p1, (uint32_t)-40) == 0);
  DEFAULT_IS(KP_OLED_R_ANIM_Y, -40);
  sim_advance(30);
  CHECK(g_layout_events == events + 1 && g_layout_last_id == KP_OLED_R_ANIM_Y);

  /* and keeps them in its own flash */
  sim_advance(SEC(4));
  struct record *r = store_find("keypaw/display/18");
  CHECK(r != NULL && r->value == -40);
}

static void s_layout_peripheral_boot(void) {
  static const struct fake_rec recs[] = {{"18", -55, 4}, {"19", 4, 4}, {"49", 7, 4}};
  boot_with(recs, 3);
  DEFAULT_IS(KP_OLED_R_ANIM_Y, -55);
  DEFAULT_IS(KP_OLED_R_ANIM_KIND, KP_OLED_ANIM_POKEMON);
  CHECK(kp_display_setting_get(KP_OLED_L_OUTPUT_X) == 0); /* no such setting here */
  CHECK(g_layout_events == 1 && g_layout_last_id == 0);
}

#endif /* CONFIG_KEYPAW_OLED_LAYOUT */

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
#ifdef CONFIG_KEYPAW_OLED_LAYOUT
    {"layout_defaults", s_layout_defaults},
    {"layout_range", s_layout_range},
    {"layout_picture", s_layout_picture},
    {"layout_event", s_layout_event},
    {"layout_persist", s_layout_persist},
    {"layout_load", s_layout_load},
    {"layout_sync", s_layout_sync},
    {"layout_command", s_layout_command},
    {"layout_peripheral", s_layout_peripheral},
    {"layout_peripheral_boot", s_layout_peripheral_boot},
#endif
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

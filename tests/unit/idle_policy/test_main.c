/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Host test of src/idle_policy.c (and of the pure rules in src/idle_policy.h and in
 * include/zmk/display_settings.h), with a fake clock. One scenario per process: the
 * module keeps its state in statics. Usage: test <scenario>; without an argument it
 * lists the scenarios. Built and run by tests/unit/test_idle_policy.py.
 *
 * The stand-ins are the ones of the display settings test (tests/unit/display_settings:
 * the clock, the work queues, USB); what the policy calls in the rest of the module is
 * recorded here.
 */
#include <errno.h>

#include "stub_all.h"

#include "display_settings.h"
#include "idle_policy.h"

void sim_advance(int64_t ms);
int64_t sim_now(void);

static int g_fail;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("  FAIL line %d: %s\n", __LINE__, #cond);                         \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

#define SEC(n) ((int64_t)(n) * 1000)

/* ---- what the policy needs from the rest of the module ---------------------- */

static int32_t g_settings[256];

int32_t kp_display_setting_get(enum kp_display_setting_id id) {
  return g_settings[(uint8_t)id];
}

struct k_work_q *kp_rgb_work_q(void) { return &g_sys_q; }

static int g_display_changed, g_rgb_changed, g_kick_key, g_kick_usb;
/* Called while the policy runs, to put a key press in the middle of it. */
static void (*g_hook_rgb)(void);

void kp_display_idle_changed(void) { g_display_changed++; }

void kp_rgb_idle_changed(void) {
  g_rgb_changed++;
  if (g_hook_rgb != NULL) {
    g_hook_rgb();
  }
}

void kp_rgb_sync_activity_kick(bool key) {
  if (key) {
    g_kick_key++;
  } else {
    g_kick_usb++;
  }
}

/* ---- helpers ----------------------------------------------------------------- */

/* The defaults of the module (Kconfig): the screen never goes off, the LEDs go off
 * after 5 minutes on battery, the screen dims after 2 minutes. */
static void defaults(void) {
  memset(g_settings, 0, sizeof(g_settings));
  g_settings[KP_DISPLAY_OFF_MODE] = KP_DISPLAY_OFF_NEVER;
  g_settings[KP_DISPLAY_OFF_AFTER_S] = 60;
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ON_BATTERY;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 300;
  g_settings[KP_DISPLAY_GLOW_OFF_MODE] = KP_DISPLAY_OFF_ON_BATTERY;
  g_settings[KP_DISPLAY_GLOW_OFF_AFTER_S] = 300;
  g_settings[KP_DISPLAY_DIM_AFTER_S] = 120;
  g_settings[KP_DISPLAY_DIM_LEVEL] = 25;
}

/* The keyboard starts, the settings are loaded, the policy reads them. */
static void boot(void) {
  defaults();
  g_sys_init_fn();
  kp_idle_config_changed();
  sim_advance(1);
}

static void key(uint8_t source) {
  zmk_event_t ev = {.kind = 1};
  ev.position.source = source;
  ev.position.position = 3;
  ev.position.state = true;
  g_listener_cb(&ev);
}

static void usb_event(void) {
  zmk_event_t ev = {.kind = 2};
  g_listener_cb(&ev);
}

static void set_usb(int on) { g_usb_powered = on; }

static void all_awake(void) {
  CHECK(!kp_idle_screen_off() && !kp_idle_keys_off() && !kp_idle_glow_off() &&
        !kp_idle_dimmed());
}

/* ---- the pure rules ---------------------------------------------------------- */

static void s_pure(void) {
  CHECK(!kp_idle_armed(KP_DISPLAY_OFF_NEVER, false));
  CHECK(!kp_idle_armed(KP_DISPLAY_OFF_NEVER, true));
  CHECK(kp_idle_armed(KP_DISPLAY_OFF_ON_BATTERY, false));
  CHECK(!kp_idle_armed(KP_DISPLAY_OFF_ON_BATTERY, true));
  CHECK(kp_idle_armed(KP_DISPLAY_OFF_ALWAYS, false));
  CHECK(kp_idle_armed(KP_DISPLAY_OFF_ALWAYS, true));
  CHECK(!kp_idle_armed(3, false)); /* a mode that does not exist is never */

  struct kp_idle_config cfg = {
      .screen_mode = KP_DISPLAY_OFF_ALWAYS,
      .screen_after_s = 60,
      .keys_mode = KP_DISPLAY_OFF_ON_BATTERY,
      .keys_after_s = 300,
      .glow_mode = KP_DISPLAY_OFF_NEVER,
      .glow_after_s = 300,
      .dim_after_s = 120,
  };
  struct kp_idle_state st;

  /* nothing has passed: the first thing to happen is the screen, in 60 s */
  CHECK(kp_idle_evaluate(&cfg, false, 0, &st) == 60000);
  CHECK(!st.screen_off && !st.keys_off && !st.glow_off && !st.dimmed);
  /* a negative time (the clock before the last activity) counts as none */
  CHECK(kp_idle_evaluate(&cfg, false, -5, &st) == 60000);
  CHECK(kp_idle_evaluate(&cfg, false, 59999, &st) == 1);
  CHECK(!st.screen_off);

  /* due at the limit, not before; the next change is the dim, 60 s away */
  CHECK(kp_idle_evaluate(&cfg, false, 60000, &st) == 60000);
  CHECK(st.screen_off && !st.keys_off && !st.glow_off && !st.dimmed);
  CHECK(kp_idle_evaluate(&cfg, false, 120000, &st) == 180000);
  CHECK(st.screen_off && st.dimmed && !st.keys_off);
  CHECK(kp_idle_evaluate(&cfg, false, 300000, &st) == KP_IDLE_NO_DEADLINE);
  CHECK(st.screen_off && st.dimmed && st.keys_off && !st.glow_off);

  /* with USB the keys do not go off (only on battery), the screen does (always) */
  CHECK(kp_idle_evaluate(&cfg, true, 300000, &st) == KP_IDLE_NO_DEADLINE);
  CHECK(st.screen_off && st.dimmed && !st.keys_off);
  CHECK(kp_idle_evaluate(&cfg, true, 0, &st) == 60000);

  /* the dim does not depend on USB, 0 is never, 1 to 9 count as 10 */
  cfg.screen_mode = KP_DISPLAY_OFF_NEVER;
  cfg.keys_mode = KP_DISPLAY_OFF_NEVER;
  cfg.dim_after_s = 0;
  CHECK(kp_idle_evaluate(&cfg, false, 999999, &st) == KP_IDLE_NO_DEADLINE);
  CHECK(!st.dimmed && !st.screen_off && !st.keys_off && !st.glow_off);
  cfg.dim_after_s = 1;
  CHECK(kp_idle_evaluate(&cfg, false, 9999, &st) == 1);
  CHECK(!st.dimmed);
  CHECK(kp_idle_evaluate(&cfg, true, 10000, &st) == KP_IDLE_NO_DEADLINE);
  CHECK(st.dimmed);
  cfg.dim_after_s = 9;
  CHECK(kp_idle_evaluate(&cfg, false, 9999, &st) == 1 && !st.dimmed);
  cfg.dim_after_s = 10;
  CHECK(kp_idle_evaluate(&cfg, false, 9999, &st) == 1 && !st.dimmed);
  cfg.dim_after_s = 11;
  CHECK(kp_idle_evaluate(&cfg, false, 10000, &st) == 1000 && !st.dimmed);

  /* the nearest deadline wins */
  cfg.dim_after_s = 0;
  cfg.keys_mode = KP_DISPLAY_OFF_ALWAYS;
  cfg.keys_after_s = 30;
  cfg.glow_mode = KP_DISPLAY_OFF_ALWAYS;
  cfg.glow_after_s = 20;
  CHECK(kp_idle_evaluate(&cfg, false, 1000, &st) == 19000);
  CHECK(kp_idle_evaluate(&cfg, false, 20000, &st) == 10000);
  CHECK(st.glow_off && !st.keys_off);

  /* the contrast of a dimmed screen */
  CHECK(kp_idle_dim_contrast(128, 25) == 32);
  CHECK(kp_idle_dim_contrast(128, 50) == 64);
  CHECK(kp_idle_dim_contrast(255, 90) == 230);
  CHECK(kp_idle_dim_contrast(255, 100) == 255);
  CHECK(kp_idle_dim_contrast(3, 5) == 1);   /* never black ... */
  CHECK(kp_idle_dim_contrast(1, 5) == 1);
  CHECK(kp_idle_dim_contrast(0, 50) == 0);  /* ... unless it is already */
  CHECK(kp_idle_dim_contrast(255, 500) == 255); /* and never past the range */
}

/* The walk of the anti burn-in shift (include/zmk/display_settings.h). */
static void s_shift_walk(void) {
  int32_t dx, dy;

  /* off: no offset */
  kp_display_shift_offset(7, 0, &dx, &dy);
  CHECK(dx == 0 && dy == 0);
  kp_display_shift_offset(7, -3, &dx, &dy);
  CHECK(dx == 0 && dy == 0);

  for (int32_t px = 1; px <= 4; px++) {
    const uint32_t side = 2u * (uint32_t)px + 1u;
    const uint32_t cells = side * side;
    const uint32_t period = 2u * (cells - 1u);
    bool seen[81] = {false};
    int32_t last_dx = 0, last_dy = 0;

    /* the middle first: the picture starts where it was drawn */
    kp_display_shift_offset(0, px, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    last_dx = dx;
    last_dy = dy;

    for (uint32_t step = 1; step <= 2u * period; step++) {
      kp_display_shift_offset(step, px, &dx, &dy);
      CHECK(dx >= -px && dx <= px && dy >= -px && dy <= px);
      /* one pixel at a time, horizontally or vertically: never a jump */
      const int32_t distance = (dx > last_dx ? dx - last_dx : last_dx - dx) +
                               (dy > last_dy ? dy - last_dy : last_dy - dy);
      if (distance != 1) {
        printf("  FAIL px %d step %u: moved %d\n", (int)px, (unsigned)step, (int)distance);
        g_fail++;
      }
      last_dx = dx;
      last_dy = dy;
      if (step <= period) {
        seen[(dy + px) * (int32_t)side + (dx + px)] = true;
      }
    }
    /* every position of the square, within one period */
    uint32_t count = 0;
    for (uint32_t i = 0; i < cells; i++) {
      count += seen[i] ? 1u : 0u;
    }
    if (count != cells) {
      printf("  FAIL px %d: %u positions of %u\n", (int)px, (unsigned)count, (unsigned)cells);
      g_fail++;
    }
    /* and it repeats: a period later it is where it was */
    kp_display_shift_offset(period, px, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    kp_display_shift_offset(period + 5, px, &dx, &dy);
    int32_t dx5, dy5;
    kp_display_shift_offset(5, px, &dx5, &dy5);
    CHECK(dx == dx5 && dy == dy5);
  }
}

/* ---- the policy ------------------------------------------------------------------ */

/* Nothing is ever turned off when every mode is never and the dim is off. */
static void s_never(void) {
  boot();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_NEVER;
  g_settings[KP_DISPLAY_GLOW_OFF_MODE] = KP_DISPLAY_OFF_NEVER;
  g_settings[KP_DISPLAY_DIM_AFTER_S] = 0;
  kp_idle_config_changed();
  sim_advance(SEC(10000));
  all_awake();
  CHECK(g_display_changed == 0 && g_rgb_changed == 0);
}

/* The defaults: the screen dims at 2 minutes, the lights go off at 5 minutes on
 * battery, nothing at all with USB (but the dim). */
static void s_defaults_on_battery(void) {
  boot();
  set_usb(0);
  sim_advance(SEC(119));
  all_awake();
  sim_advance(SEC(2));
  CHECK(kp_idle_dimmed() && !kp_idle_screen_off() && !kp_idle_keys_off());
  CHECK(g_display_changed == 1 && g_rgb_changed == 0);
  sim_advance(SEC(170));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(11));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off() && !kp_idle_screen_off());
  /* both LEDs groups changed in one evaluation: one call */
  CHECK(g_rgb_changed == 1 && g_display_changed == 1);
  /* it stays like that */
  sim_advance(SEC(5000));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off() && kp_idle_dimmed());
  CHECK(g_rgb_changed == 1 && g_display_changed == 1);
}

static void s_defaults_on_usb(void) {
  boot();
  set_usb(1);
  usb_event();
  sim_advance(1);
  const int rgb = g_rgb_changed;
  sim_advance(SEC(5000));
  CHECK(!kp_idle_keys_off() && !kp_idle_glow_off() && !kp_idle_screen_off());
  CHECK(kp_idle_dimmed()); /* the dim has nothing to do with USB */
  CHECK(g_rgb_changed == rgb);
}

/* Plugging the cable wakes what is off and keeps the lights on; unplugging it
 * starts the time again. */
static void s_usb_plug(void) {
  boot();
  set_usb(0);
  sim_advance(SEC(400));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off() && kp_idle_dimmed());
  CHECK(g_kick_usb == 0);

  set_usb(1);
  usb_event();
  sim_advance(1);
  all_awake();
  CHECK(g_kick_usb == 1); /* the peripheral is told */
  CHECK(g_kick_key == 0);
  sim_advance(SEC(1000));
  CHECK(!kp_idle_keys_off() && !kp_idle_glow_off());

  set_usb(0);
  usb_event();
  sim_advance(1);
  CHECK(g_kick_usb == 2);
  sim_advance(SEC(299));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(2));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off());
}

static void s_always(void) {
  boot();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 60;
  g_settings[KP_DISPLAY_GLOW_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_GLOW_OFF_AFTER_S] = 90;
  g_settings[KP_DISPLAY_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_OFF_AFTER_S] = 30;
  set_usb(1);
  kp_idle_config_changed();
  sim_advance(1);
  all_awake();

  sim_advance(SEC(30));
  CHECK(kp_idle_screen_off() && !kp_idle_keys_off() && !kp_idle_glow_off());
  sim_advance(SEC(30));
  CHECK(kp_idle_keys_off() && !kp_idle_glow_off());
  sim_advance(SEC(30));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off() && kp_idle_screen_off());
}

/* The three have their own time. The screen at 30 s with the dim at 10 s. */
static void s_order(void) {
  boot();
  g_settings[KP_DISPLAY_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_OFF_AFTER_S] = 30;
  g_settings[KP_DISPLAY_DIM_AFTER_S] = 3; /* counts as 10 */
  kp_idle_config_changed();
  sim_advance(1);
  sim_advance(SEC(9));
  CHECK(!kp_idle_dimmed());
  sim_advance(SEC(1));
  CHECK(kp_idle_dimmed() && !kp_idle_screen_off());
  sim_advance(SEC(20));
  CHECK(kp_idle_dimmed() && kp_idle_screen_off());
  CHECK(g_display_changed == 2);
}

/* Activity before the time is up moves the time: the lights go off a full limit after
 * the last key, not after the first. */
static void s_activity_postpones(void) {
  boot();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 60;
  kp_idle_config_changed();
  sim_advance(1);

  sim_advance(SEC(50));
  key(ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL);
  sim_advance(SEC(11)); /* t = 61: the first deadline passed */
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(48));
  CHECK(!kp_idle_keys_off()); /* t = 109, 59 s after the key */
  sim_advance(SEC(2));
  CHECK(kp_idle_keys_off());
  CHECK(g_rgb_changed == 1);
}

/* A key wakes everything at once, and tells the peripheral only about its own. */
static void s_wake_and_kick(void) {
  boot();
  set_usb(0);
  sim_advance(SEC(400));
  CHECK(kp_idle_keys_off() && kp_idle_glow_off() && kp_idle_dimmed());
  const int display = g_display_changed;
  const int rgb = g_rgb_changed;

  /* a key of the peripheral: it wakes, and nothing is sent back */
  key(0);
  sim_advance(1);
  all_awake();
  CHECK(g_rgb_changed == rgb + 1 && g_display_changed == display + 1);
  CHECK(g_kick_key == 0);

  /* and again off, then a key of this half: it wakes and the peripheral is told */
  sim_advance(SEC(400));
  CHECK(kp_idle_keys_off());
  key(ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL);
  sim_advance(1);
  all_awake();
  CHECK(g_kick_key == 1);
}

/* On a peripheral the central says whether the keyboard has USB; it is believed for
 * a while, and not after that. */
static void s_peer_usb(void) {
  boot();
  set_usb(0);
  kp_idle_peer_update(true, false);
  sim_advance(1);
  sim_advance(SEC(119));
  CHECK(!kp_idle_keys_off());
  /* 120 s on, the central stopped saying it: the keyboard has no USB (as far as this
   * half knows), and the time that has passed counts */
  sim_advance(SEC(2));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(170)); /* t = 291 */
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(15)); /* t = 306 */
  CHECK(kp_idle_keys_off() && kp_idle_glow_off());

  /* it says it again: the lights come back and stay */
  kp_idle_peer_update(true, false);
  sim_advance(1);
  CHECK(!kp_idle_keys_off() && !kp_idle_glow_off());
  sim_advance(SEC(115));
  CHECK(!kp_idle_keys_off());

  /* it says the cable is out: that is somebody being there, and the time counts
   * from now */
  kp_idle_peer_update(false, false);
  sim_advance(1);
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(299));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(2));
  CHECK(kp_idle_keys_off());
}

/* The first report after the start is not a change: it wakes nothing. */
static void s_peer_first_report(void) {
  boot();
  set_usb(0);
  sim_advance(SEC(400));
  CHECK(kp_idle_keys_off());
  const int rgb = g_rgb_changed;

  /* no USB on the central, as this half assumed: nothing to wake */
  kp_idle_peer_update(false, false);
  sim_advance(1);
  CHECK(kp_idle_keys_off());
  CHECK(g_rgb_changed == rgb);
  /* the same again, as the clock of the sync repeats it */
  kp_idle_peer_update(false, false);
  sim_advance(1);
  CHECK(kp_idle_keys_off() && g_rgb_changed == rgb);
}

/* What the central reports as a key is activity on the peripheral. */
static void s_peer_key(void) {
  boot();
  set_usb(0);
  sim_advance(SEC(250));
  kp_idle_peer_update(false, true);
  sim_advance(SEC(100)); /* t = 350: more than 300 s, but 100 s after the key */
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(201)); /* t = 551 */
  CHECK(kp_idle_keys_off());
  /* a report without a key is not activity */
  kp_idle_peer_update(false, false);
  sim_advance(1);
  CHECK(kp_idle_keys_off());
}

/* A change of the settings counts at once, in either direction. */
static void s_config(void) {
  boot();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_NEVER;
  kp_idle_config_changed();
  sim_advance(SEC(200));
  CHECK(!kp_idle_keys_off());

  /* from never to always at 100 s, with 200 s gone: off at once */
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 100;
  kp_idle_config_changed();
  sim_advance(1);
  CHECK(kp_idle_keys_off());

  /* a longer time: on again at once */
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 600;
  kp_idle_config_changed();
  sim_advance(1);
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(399));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(2));
  CHECK(kp_idle_keys_off());

  /* back to never */
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_NEVER;
  kp_idle_config_changed();
  sim_advance(1);
  CHECK(!kp_idle_keys_off());
}

static bool g_press_fired;

/* A key comes in while the policy is telling the LEDs that they go off. */
static void press_during_evaluation(void) {
  if (!g_press_fired) {
    g_press_fired = true;
    kp_idle_note_activity();
  }
}

/* Nothing is lost when a key comes in the middle of an evaluation: the policy looks
 * again at once and finds the lights have to be on. */
static void s_key_during_evaluation(void) {
  boot();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 60;
  kp_idle_config_changed();
  sim_advance(1);

  g_hook_rgb = press_during_evaluation;
  sim_advance(SEC(61));
  g_hook_rgb = NULL;

  /* off, then on again right away */
  CHECK(g_press_fired);
  CHECK(!kp_idle_keys_off());
  CHECK(g_rgb_changed == 2);
  /* and the time counts from that key, which came at t = 60 s */
  sim_advance(SEC(58));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(2));
  CHECK(kp_idle_keys_off());
}

/* A keyboard with no saved settings: nobody asks the policy to read them, so it does
 * by itself, a little after the start. */
static void s_no_saved_settings(void) {
  defaults();
  g_settings[KP_DISPLAY_KEYS_OFF_MODE] = KP_DISPLAY_OFF_ALWAYS;
  g_settings[KP_DISPLAY_KEYS_OFF_AFTER_S] = 10;
  g_sys_init_fn();
  sim_advance(SEC(1));
  CHECK(!kp_idle_keys_off());
  sim_advance(SEC(30));
  CHECK(kp_idle_keys_off());
}

struct scenario {
  const char *name;
  void (*fn)(void);
};

static const struct scenario scenarios[] = {
    {"pure", s_pure},
    {"shift_walk", s_shift_walk},
    {"never", s_never},
    {"defaults_on_battery", s_defaults_on_battery},
    {"defaults_on_usb", s_defaults_on_usb},
    {"usb_plug", s_usb_plug},
    {"always", s_always},
    {"order", s_order},
    {"activity_postpones", s_activity_postpones},
    {"wake_and_kick", s_wake_and_kick},
    {"peer_usb", s_peer_usb},
    {"peer_first_report", s_peer_first_report},
    {"peer_key", s_peer_key},
    {"config", s_config},
    {"key_during_evaluation", s_key_during_evaluation},
    {"no_saved_settings", s_no_saved_settings},
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

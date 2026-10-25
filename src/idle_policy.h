/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * When the screen and the lights go off, and when the screen dims
 * (CONFIG_KEYPAW_DISPLAY_SETTINGS; idle_policy.c is the glue, this file holds what
 * does not need the kernel, so a test on the PC can include it).
 *
 * Four things follow the time without activity, each with its own limit:
 *
 *   - the screen goes off     (settings KP_DISPLAY_OFF_MODE, KP_DISPLAY_OFF_AFTER_S),
 *   - the LEDs under the keys (KP_DISPLAY_KEYS_OFF_MODE, KP_DISPLAY_KEYS_OFF_AFTER_S),
 *   - the underglow LEDs      (KP_DISPLAY_GLOW_OFF_MODE, KP_DISPLAY_GLOW_OFF_AFTER_S),
 *   - the screen dims         (KP_DISPLAY_DIM_AFTER_S, KP_DISPLAY_DIM_LEVEL).
 *
 * "Off" has a mode: never, only on battery (while the keyboard has no USB) or
 * always. The dimming does not depend on USB: it is the anti burn-in of a screen
 * that never goes off.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk/display_settings.h>

/* What kp_idle_evaluate() returns when nothing changes with the passing of time. */
#define KP_IDLE_NO_DEADLINE UINT32_MAX

/* The settings that the evaluation reads, in seconds and modes. */
struct kp_idle_config {
  uint8_t screen_mode; /* enum kp_display_off_mode */
  uint32_t screen_after_s;
  uint8_t keys_mode;
  uint32_t keys_after_s;
  uint8_t glow_mode;
  uint32_t glow_after_s;
  /* 0 is never; a value from 1 to 9 counts as 10. */
  uint32_t dim_after_s;
};

/* What the evaluation says is the case now. */
struct kp_idle_state {
  bool screen_off;
  bool keys_off;
  bool glow_off;
  bool dimmed;
};

/* True when a thing with this mode may go off at all: always in the mode "always",
 * and in "only on battery" while the keyboard has no USB. Pure. */
static inline bool kp_idle_armed(uint8_t mode, bool usb) {
  switch (mode) {
  case KP_DISPLAY_OFF_ALWAYS:
    return true;
  case KP_DISPLAY_OFF_ON_BATTERY:
    return !usb;
  default:
    return false;
  }
}

/* True when an armed thing has been idle for `after_s` seconds. Otherwise lowers
 * `*next` to the milliseconds left, if that is less. */
static inline bool kp_idle_due(bool armed, uint32_t after_s, int64_t idle_ms,
                               uint32_t *next) {
  if (!armed) {
    return false;
  }

  const int64_t limit = (int64_t)after_s * 1000;

  if (idle_ms >= limit) {
    return true;
  }

  const uint32_t left = (uint32_t)(limit - idle_ms);

  if (left < *next) {
    *next = left;
  }
  return false;
}

/* Which things are off or dimmed after `idle_ms` milliseconds without activity,
 * with or without USB. Returns the milliseconds until the answer next changes
 * because time passed (never 0), or KP_IDLE_NO_DEADLINE when it will not. A change
 * of the settings, of USB or any activity asks again. Pure. */
static inline uint32_t kp_idle_evaluate(const struct kp_idle_config *cfg, bool usb,
                                        int64_t idle_ms, struct kp_idle_state *out) {
  uint32_t next = KP_IDLE_NO_DEADLINE;

  if (idle_ms < 0) {
    idle_ms = 0;
  }

  out->screen_off = kp_idle_due(kp_idle_armed(cfg->screen_mode, usb),
                                cfg->screen_after_s, idle_ms, &next);
  out->keys_off = kp_idle_due(kp_idle_armed(cfg->keys_mode, usb),
                              cfg->keys_after_s, idle_ms, &next);
  out->glow_off = kp_idle_due(kp_idle_armed(cfg->glow_mode, usb),
                              cfg->glow_after_s, idle_ms, &next);
  out->dimmed = kp_idle_due(cfg->dim_after_s > 0,
                            cfg->dim_after_s < 10 ? 10 : cfg->dim_after_s, idle_ms,
                            &next);
  return next;
}

/* The contrast of a dimmed screen: `level` percent of `base`, and never 0 unless
 * `base` is (the panel stays readable). Pure. */
static inline uint8_t kp_idle_dim_contrast(uint8_t base, uint32_t level) {
  if (base == 0) {
    return 0;
  }

  const uint32_t scaled = ((uint32_t)base * level + 50u) / 100u;

  if (scaled < 1u) {
    return 1;
  }
  return scaled > 255u ? 255 : (uint8_t)scaled;
}

/* ---- the glue (idle_policy.c) --------------------------------------------- */

/* Something happened that counts as activity: a key press on either half, a change
 * made in ZMK Studio, USB plugged in. Any context; cheap. */
void kp_idle_note_activity(void);
/* A setting or the USB state changed: evaluate again at once. Any context. */
void kp_idle_config_changed(void);
/* What the central says (RGB_ACTIVITY_CMD), as the peripheral receives it:
 * whether the central has USB power and whether a key was pressed there just now. */
void kp_idle_peer_update(bool usb, bool key);

/* The state evaluated last; any context. */
bool kp_idle_screen_off(void);
bool kp_idle_keys_off(void);
bool kp_idle_glow_off(void);
bool kp_idle_dimmed(void);
/* USB power on this half (what the central reports is not counted here). */
bool kp_idle_usb_local(void);

/* What the glue calls when the state changed: implemented by display_settings.c
 * (the screen) and rgb_matrix.c (the LEDs). */
void kp_display_idle_changed(void);
void kp_rgb_idle_changed(void);

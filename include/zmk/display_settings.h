/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Display (OLED) settings (CONFIG_KEYPAW_DISPLAY_SETTINGS): numbered integers
 * that ZMK Studio edits (the display requests of lenerdz/zmk-studio-messages)
 * and that the module applies, keeps in flash and pushes to a split peripheral.
 * A screen widget reads them with kp_display_setting_get().
 */

#pragma once

#include <stdint.h>

/* The ids are part of the ZMK Studio protocol (README of
 * lenerdz/zmk-studio-messages) and of the command the central sends to its
 * peripheral, which holds the id in 4 bits: keep every id below 16. */
enum kp_display_setting_id {
  /* 0 to 255: contrast of the left (central) screen. */
  KP_DISPLAY_LEFT_CONTRAST = 1,
  /* 0 to 255: contrast of the right (peripheral) screen. */
  KP_DISPLAY_RIGHT_CONTRAST = 2,
  /* enum kp_display_off_mode: when the screen goes off after the keyboard is
   * idle. */
  KP_DISPLAY_OFF_MODE = 3,
  /* Seconds without activity before the screen goes off (not less than
   * CONFIG_ZMK_IDLE_TIMEOUT). */
  KP_DISPLAY_OFF_AFTER_S = 4,
  /* 0 or 1: the light toast on the right screen. */
  KP_DISPLAY_TOAST = 5,
  /* 500 to 5000: how long the toast stays on screen, in milliseconds. */
  KP_DISPLAY_TOAST_MS = 6,
};

/* How many settings there are. */
#define KP_DISPLAY_SETTING_COUNT 6

enum kp_display_off_mode {
  KP_DISPLAY_OFF_NEVER = 0,
  /* Only while USB is not connected to this half. */
  KP_DISPLAY_OFF_ON_BATTERY = 1,
  KP_DISPLAY_OFF_ALWAYS = 2,
};

/* The current value of a setting, or 0 for an id that does not exist. Any
 * context; the value is whatever was loaded, received or set last. */
int32_t kp_display_setting_get(enum kp_display_setting_id id);

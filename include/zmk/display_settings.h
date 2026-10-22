/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Display (OLED) settings (CONFIG_KEYPAW_DISPLAY_SETTINGS): numbered integers
 * that ZMK Studio edits (the display requests of lenerdz/zmk-studio-messages)
 * and that the module applies, keeps in flash and pushes to a split peripheral.
 * A screen widget reads them with kp_display_setting_get().
 *
 * With CONFIG_KEYPAW_OLED_LAYOUT the settings also say which widgets of the
 * zmk-nice-oled screens are drawn and where (the OLED layout, ids 16 and up).
 */

#pragma once

#include <stdint.h>

/* The ids are part of the ZMK Studio protocol (README of
 * lenerdz/zmk-studio-messages) and of the command the central sends to its
 * peripheral, which holds the id in 8 bits: keep every id below 256. */
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

  /* ---- OLED layout (CONFIG_KEYPAW_OLED_LAYOUT) ------------------------------
   *
   * One block of eight ids per widget of the zmk-nice-oled screens, at
   * 16 + 8 * n. In a block, +0 is "shown" (0 or 1), +1 the x and +2 the y of the
   * widget, +3 its kind and +4 its cycle time, for the widgets that have them.
   * x and y are in pixels of the 68 x 160 canvas of the widget (see below);
   * KP_OLED_COORD_MIN to KP_OLED_COORD_MAX. A widget that the firmware was not
   * built with has no entry in the list that ZMK Studio gets.
   *
   * The canvas is drawn upright and the screen shows part of it, x 0 to 31 and
   * y 32 to 159. The right-screen animation is the exception: it is an image
   * that is already rotated, placed on the rotated canvas, so its x and y are
   * that placement (see README: "Coordinates of the animation").
   */

  /* Right screen (the peripheral half). */
  KP_OLED_R_ANIM_SHOW = 16,
  KP_OLED_R_ANIM_X = 17,
  KP_OLED_R_ANIM_Y = 18,
  /* enum kp_oled_anim_kind: which animation or image. */
  KP_OLED_R_ANIM_KIND = 19,
  /* 0, or 100 to 60000: how long one cycle of the animation takes, in
   * milliseconds. 0 is the time that belongs to the animation (for the one the
   * build chose, CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_MS). A value from
   * 1 to 99 counts as 100. */
  KP_OLED_R_ANIM_MS = 20,

  /* The battery level of the right half, as a percentage. */
  KP_OLED_R_BATTERY_SHOW = 24,
  KP_OLED_R_BATTERY_X = 25,
  KP_OLED_R_BATTERY_Y = 26,

  /* The Bluetooth icon: connected to the left half or not. */
  KP_OLED_R_LINK_SHOW = 32,
  KP_OLED_R_LINK_X = 33,
  KP_OLED_R_LINK_Y = 34,

  /* The first line of the light toast; the second one is 8 pixels below. Whether
   * the toast is shown at all is KP_DISPLAY_TOAST. */
  KP_OLED_R_TOAST_X = 41,
  KP_OLED_R_TOAST_Y = 42,

  /* Left screen (the central half). */

  /* The USB or Bluetooth icon of the active output. */
  KP_OLED_L_OUTPUT_SHOW = 48,
  KP_OLED_L_OUTPUT_X = 49,
  KP_OLED_L_OUTPUT_Y = 50,

  /* The battery: the icon or percentage of this half, or the two numbers of
   * both halves. */
  KP_OLED_L_BATTERY_SHOW = 56,
  KP_OLED_L_BATTERY_X = 57,
  KP_OLED_L_BATTERY_Y = 58,

  /* The five Bluetooth profile dots, with the active one marked. */
  KP_OLED_L_PROFILE_SHOW = 64,
  KP_OLED_L_PROFILE_X = 65,
  KP_OLED_L_PROFILE_Y = 66,

  /* The number of the active Bluetooth profile. */
  KP_OLED_L_PROFILE_NUM_SHOW = 72,
  KP_OLED_L_PROFILE_NUM_X = 73,
  KP_OLED_L_PROFILE_NUM_Y = 74,

  /* The name of the active layer. */
  KP_OLED_L_LAYER_SHOW = 80,
  KP_OLED_L_LAYER_X = 81,
  KP_OLED_L_LAYER_Y = 82,

  /* The WPM speedometer: the gauge and its needle. x and y are the gauge. */
  KP_OLED_L_WPM_GAUGE_SHOW = 88,
  KP_OLED_L_WPM_GAUGE_X = 89,
  KP_OLED_L_WPM_GAUGE_Y = 90,

  /* The WPM number. */
  KP_OLED_L_WPM_NUMBER_SHOW = 96,
  KP_OLED_L_WPM_NUMBER_X = 97,
  KP_OLED_L_WPM_NUMBER_Y = 98,

  /* The WPM graph. x and y are the grid under it. */
  KP_OLED_L_WPM_GRAPH_SHOW = 104,
  KP_OLED_L_WPM_GRAPH_X = 105,
  KP_OLED_L_WPM_GRAPH_Y = 106,
};

/* The most settings the module keeps: the size of its bitmap of changes and of
 * the copy the central holds of what a peripheral has. The protocol sets no
 * limit of its own (DisplayInfo.settings is encoded by a callback, see
 * studio_rpc_display.c). A build with more entries fails a BUILD_ASSERT. */
#define KP_DISPLAY_SETTING_CAP 64

/* The range of every x and y of the OLED layout. */
#define KP_OLED_COORD_MIN (-160)
#define KP_OLED_COORD_MAX 320

/* The values of KP_OLED_R_ANIM_KIND, in the order the app offers them. */
enum kp_oled_anim_kind {
  KP_OLED_ANIM_CRYSTAL = 0, /* 16 frames; what CONFIG_..._PERIPHERAL_GEM draws */
  KP_OLED_ANIM_CAT = 1,     /* 8 frames */
  KP_OLED_ANIM_HEAD = 2,    /* 16 frames */
  KP_OLED_ANIM_SPACEMAN = 3, /* 20 frames */
  KP_OLED_ANIM_POKEMON = 4, /* 48 frames */
  KP_OLED_ANIM_VIM = 5,     /* a still image */
  KP_OLED_ANIM_VIP_MARCOS = 6, /* a still image */
};

#define KP_OLED_ANIM_KIND_COUNT 7

enum kp_display_off_mode {
  KP_DISPLAY_OFF_NEVER = 0,
  /* Only while USB is not connected to this half. */
  KP_DISPLAY_OFF_ON_BATTERY = 1,
  KP_DISPLAY_OFF_ALWAYS = 2,
};

/* The current value of a setting, or 0 for an id that does not exist. Any
 * context; the value is whatever was loaded, received or set last. */
int32_t kp_display_setting_get(enum kp_display_setting_id id);

/* The default of a setting (what restoring the defaults sets; for the OLED
 * layout, what the build says), or 0 for an id that does not exist. */
int32_t kp_display_setting_default(enum kp_display_setting_id id);

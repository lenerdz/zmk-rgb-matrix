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

#include <stdbool.h>
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
  /* Seconds without activity before the screen goes off, 10 to 3600. The module
   * counts the idle time itself (idle_policy.c), so it no longer depends on
   * CONFIG_ZMK_IDLE_TIMEOUT. */
  KP_DISPLAY_OFF_AFTER_S = 4,
  /* 0 or 1: the light toast on the right screen. */
  KP_DISPLAY_TOAST = 5,
  /* 500 to 5000: how long the toast stays on screen, in milliseconds. */
  KP_DISPLAY_TOAST_MS = 6,

  /* ---- When the lights go off, and the anti burn-in (ids 7 to 15) -----------
   *
   * The same idle time counts for everything below and for the screen (ids 3 and
   * 4): the time since the last key press on either half, the use of ZMK Studio
   * and plugging USB in. The central tells the peripheral about it (the command
   * RGB_ACTIVITY_CMD), so the two halves turn off together whichever half is
   * being typed on. For the modes "only on battery", the keyboard has USB when
   * this half has it or, on the peripheral, when the central has it. */

  /* enum kp_display_off_mode: when the LEDs under the keys go off. */
  KP_DISPLAY_KEYS_OFF_MODE = 7,
  /* Seconds without activity before they go off, 10 to 3600. */
  KP_DISPLAY_KEYS_OFF_AFTER_S = 8,
  /* enum kp_display_off_mode: when the underglow LEDs (the ones that are not
   * under a key) go off. */
  KP_DISPLAY_GLOW_OFF_MODE = 9,
  /* Seconds without activity before they go off, 10 to 3600. */
  KP_DISPLAY_GLOW_OFF_AFTER_S = 10,
  /* 0 or 1: pixel shift. Every KP_DISPLAY_SHIFT_EVERY_S seconds the picture of
   * each screen moves one pixel, along a walk that is KP_DISPLAY_SHIFT_PX pixels
   * wide on each side (kp_display_shift_offset), so that no pixel stays lit under
   * the same picture for hours. The screens do it themselves (zmk-nice-oled). */
  KP_DISPLAY_SHIFT = 11,
  /* 10 to 3600: seconds between two steps of the pixel shift. */
  KP_DISPLAY_SHIFT_EVERY_S = 12,
  /* 1 to 4: how far the picture goes from its place, in pixels, on each side. */
  KP_DISPLAY_SHIFT_PX = 13,
  /* 0 to 3600: seconds without activity before the screen dims (its contrast
   * drops to KP_DISPLAY_DIM_LEVEL percent). 0 is never; a value from 1 to 9 counts
   * as 10. The screen can go off (ids 3 and 4) after dimming, or stay dimmed. */
  KP_DISPLAY_DIM_AFTER_S = 14,
  /* 5 to 90: the contrast of a dimmed screen, as a percentage of the one set in
   * KP_DISPLAY_LEFT_CONTRAST or KP_DISPLAY_RIGHT_CONTRAST. */
  KP_DISPLAY_DIM_LEVEL = 15,

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
   * that placement (see README: "Coordinates of the animation"). With a
   * rotation (KP_OLED_R_ANIM_ROT) they are the placement of the turned picture.
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

  /* More of the right-screen animation (or image), whose block (16 to 23) is
   * full of what the first version needed.
   *
   * 0 to 3: quarter turns clockwise, as the picture is looked at. */
  KP_OLED_R_ANIM_ROT = 112,
  /* enum kp_oled_mix: how a lit pixel of the picture mixes with the widgets
   * that are under it. */
  KP_OLED_R_ANIM_MIX = 113,
  /* The piece of the picture that shows, in pixels of the picture as it is
   * looked at (after ROT), from its top left corner. Two numbers in each
   * setting, 8 bits apiece, so that a change of the piece is one write:
   *   CROP_XY = x | y << 8         0 to 65535
   *   CROP_WH = width | height << 8
   * A width or a height of 0 (the default) is the whole picture. What goes
   * past the picture is cut off. */
  KP_OLED_R_ANIM_CROP_XY = 114,
  KP_OLED_R_ANIM_CROP_WH = 115,
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
  KP_OLED_ANIM_VIM = 5, /* retired: was a still image */
  KP_OLED_ANIM_VIP_MARCOS = 6, /* retired: was a still image */

  /* The pictures that scripts/pictures/make_pictures.py of zmk-nice-oled draws (the
   * list is assets/pictures/pictures.json there), 32 pixels wide: the width of the
   * screen, so that none of it is cut off by the panel. A number is what the keyboard
   * saves in its memory: only add at the end, never reorder or remove.
   *
   * A kind can be retired: the picture is gone from the firmware and from the app,
   * and its number stays reserved so that the others keep theirs. Nothing is ever
   * drawn for a retired kind, the keyboard does not accept it as a setting and it
   * forgets one that it had saved (kp_oled_anim_kind_retired()). Of the still
   * images only 18 (mountains), 19 (skyline), 20 (circuit), 23 (keyboard), 49
   * (moon cat) and 51 (planet) are left; the 24 other ones are retired. */
  KP_OLED_ANIM_EQUALIZER = 7,  /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_RADAR = 8,      /* 24 frames, 32 x 32 */
  KP_OLED_ANIM_HEARTBEAT = 9,  /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_RAIN = 10,      /* 32 frames, 32 x 128 */
  KP_OLED_ANIM_CUBE = 11,      /* 16 frames, 32 x 32 */
  KP_OLED_ANIM_ATOM = 12,      /* 24 frames, 32 x 32 */
  KP_OLED_ANIM_HELIX = 13,     /* 16 frames, 32 x 128 */
  KP_OLED_ANIM_WARP = 14,      /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_SONAR = 15,     /* 16 frames, 32 x 32 */
  KP_OLED_ANIM_BOUNCE = 16,    /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_FLAME = 17,     /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_MOUNTAINS = 18, /* a still image, 32 x 128 */
  KP_OLED_ANIM_SKYLINE = 19,   /* a still image, 32 x 128 */
  KP_OLED_ANIM_CIRCUIT = 20,   /* a still image, 32 x 128 */
  KP_OLED_ANIM_ALIEN = 21, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_HEART = 22, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_KEYBOARD = 23,  /* a still image, 32 x 64 */
  KP_OLED_ANIM_PULSAR = 24, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_ROCKET = 25, /* retired: was a still image, 32 x 64 */

  /* The second batch: 20 animations (26 to 45), then 20 still images (46 to 65), with
   * the same rules. */
  KP_OLED_ANIM_WATERFALL = 26,  /* 16 frames, 32 x 128 */
  KP_OLED_ANIM_LAVA = 27,       /* 16 frames, 32 x 128 */
  KP_OLED_ANIM_LIGHTNING = 28,  /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_SNOW = 29,       /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_JELLYFISH = 30,  /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_TETRIS = 31,     /* 20 frames, 32 x 64 */
  KP_OLED_ANIM_PONG = 32,       /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_INVADERS = 33,   /* 8 frames, 32 x 64 */
  KP_OLED_ANIM_PACMAN = 34,     /* 16 frames, 32 x 32 */
  KP_OLED_ANIM_LIFE = 35,       /* 30 frames, 32 x 64 */
  KP_OLED_ANIM_PLASMA = 36,     /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_SPIRAL = 37,     /* 16 frames, 32 x 32 */
  KP_OLED_ANIM_NEWTON = 38,     /* 16 frames, 32 x 32 */
  KP_OLED_ANIM_GEARS = 39,      /* 12 frames, 32 x 32 */
  KP_OLED_ANIM_HOURGLASS = 40,  /* 20 frames, 32 x 64 */
  KP_OLED_ANIM_WINDMILL = 41,   /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_FIREWORKS = 42,  /* 24 frames, 32 x 64 */
  KP_OLED_ANIM_UFO = 43,        /* 16 frames, 32 x 64 */
  KP_OLED_ANIM_EYE = 44,        /* 24 frames, 32 x 32 */
  KP_OLED_ANIM_KEYWAVE = 45,    /* 24 frames, 32 x 64 */

  KP_OLED_ANIM_COFFEE = 46, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_CACTUS = 47, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_LIGHTHOUSE = 48, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_MOON_CAT = 49,   /* a still image, 32 x 64 */
  KP_OLED_ANIM_ROBOT = 50, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_PLANET = 51,     /* a still image, 32 x 64 */
  KP_OLED_ANIM_CASTLE = 52, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_TREE = 53, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_SKULL = 54, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_SWORD = 55, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_KEY = 56, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_ANTENNA = 57, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_POTION = 58, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_SAILBOAT = 59, /* retired: was a still image, 32 x 64 */
  KP_OLED_ANIM_COMPASS = 60, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_CROWN = 61, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_CASSETTE = 62, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_FLOPPY = 63, /* retired: was a still image, 32 x 32 */
  KP_OLED_ANIM_PHASES = 64, /* retired: was a still image, 32 x 128 */
  KP_OLED_ANIM_MUSHROOM = 65, /* retired: was a still image, 32 x 32 */
};

#define KP_OLED_ANIM_KIND_COUNT 66

/* The values of KP_OLED_R_ANIM_MIX. */
enum kp_oled_mix {
  /* Both stay lit where a widget and the picture overlap. */
  KP_OLED_MIX_ADD = 0,
  /* The picture flips what is under it: a widget shows in reverse where it is
   * over a lit part of the picture, and stays readable. */
  KP_OLED_MIX_INVERT = 1,
};

/* The values of the settings that say when something goes off (ids 3, 7 and 9). */
enum kp_display_off_mode {
  KP_DISPLAY_OFF_NEVER = 0,
  /* Only while USB is not connected to this half; on the peripheral, to the
   * keyboard: the central counts too. */
  KP_DISPLAY_OFF_ON_BATTERY = 1,
  KP_DISPLAY_OFF_ALWAYS = 2,
};

/* True for a kind of KP_OLED_R_ANIM_KIND that was retired: it has a number but no
 * picture. Pure; usable from any context. */
static inline bool kp_oled_anim_kind_retired(int32_t kind) {
  switch (kind) {
  case KP_OLED_ANIM_VIM:
  case KP_OLED_ANIM_VIP_MARCOS:
  case KP_OLED_ANIM_ALIEN:
  case KP_OLED_ANIM_HEART:
  case KP_OLED_ANIM_PULSAR:
  case KP_OLED_ANIM_ROCKET:
  case KP_OLED_ANIM_COFFEE:
  case KP_OLED_ANIM_CACTUS:
  case KP_OLED_ANIM_LIGHTHOUSE:
  case KP_OLED_ANIM_ROBOT:
    return true;
  default:
    /* 52 to 65: castle, tree, skull, sword, key, antenna, potion, sailboat,
     * compass, crown, cassette, floppy, phases and mushroom. */
    return kind >= KP_OLED_ANIM_CASTLE && kind <= KP_OLED_ANIM_MUSHROOM;
  }
}

/* Where the screens put their picture at step `step` of the pixel shift
 * (KP_DISPLAY_SHIFT), as the offset (dx, dy) from its place, in pixels: at most `px`
 * (1 to 4) to each side. The walk goes over the (2 * px + 1) x (2 * px + 1) squares
 * row by row (the odd rows from the right) and then back the way it came, so the
 * picture only ever moves one pixel at a time (nothing jumps) and every square gets
 * its turn. Step 0 is the middle: the picture is where it started. A `px` of 0 or
 * less keeps the picture where it is. Pure. */
static inline void kp_display_shift_offset(uint32_t step, int32_t px, int32_t *dx,
                                           int32_t *dy) {
  if (px <= 0) {
    *dx = 0;
    *dy = 0;
    return;
  }

  const uint32_t side = 2u * (uint32_t)px + 1u;
  const uint32_t cells = side * side;
  const uint32_t period = 2u * (cells - 1u);
  /* The middle square, whichever way its row is walked. */
  const uint32_t middle = (uint32_t)px * side + (uint32_t)px;
  uint32_t k = (step % period + middle) % period;

  /* Out along the walk, then back the way it came. */
  if (k >= cells) {
    k = period - k;
  }

  const uint32_t row = k / side;
  uint32_t col = k % side;

  if (row & 1u) {
    col = side - 1u - col;
  }

  *dx = (int32_t)col - px;
  *dy = (int32_t)row - px;
}

/* The current value of a setting, or 0 for an id that does not exist. Any
 * context; the value is whatever was loaded, received or set last. */
int32_t kp_display_setting_get(enum kp_display_setting_id id);

/* The default of a setting (what restoring the defaults sets; for the OLED
 * layout, what the build says), or 0 for an id that does not exist. */
int32_t kp_display_setting_default(enum kp_display_setting_id id);

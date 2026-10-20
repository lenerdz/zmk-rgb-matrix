/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>

/* What a user command changed. One command changes one thing, so the event
 * names the first of these that differs (effect first, period last). */
enum zmk_rgb_matrix_change_kind {
  ZMK_RGB_MATRIX_CHANGE_EFFECT,     /* another effect became the active one */
  ZMK_RGB_MATRIX_CHANGE_POWER,      /* the user turned the lights on or off */
  ZMK_RGB_MATRIX_CHANGE_BRIGHTNESS, /* brightness up or down */
  ZMK_RGB_MATRIX_CHANGE_HUE,        /* hue up or down */
  ZMK_RGB_MATRIX_CHANGE_SATURATION, /* saturation up or down */
  ZMK_RGB_MATRIX_CHANGE_PERIOD,     /* the active effect's period changed */
  ZMK_RGB_MATRIX_CHANGE_RESET,      /* the state went back to the defaults */
};

/* Raised, on the half that applied the command, after a &kprgb or &fx_*
 * binding changed the state. A split keyboard raises it on both halves (the
 * binding is global), which is what lets a peripheral's screen show it.
 *
 * It is not raised for the state that the central pushes to a peripheral when it
 * connects, nor for the colour-map edits of ZMK Studio, nor at boot: only for
 * what the user asked for. The fields describe the state after the change.
 *
 * Listeners run in the thread that applied the command. Keep them short and do
 * not call back into the module from them. */
struct zmk_rgb_matrix_changed {
  enum zmk_rgb_matrix_change_kind kind;
  /* The user's on/off intent. */
  bool on;
  /* Active effect: its position among the children of the controller (0 based),
   * how many there are, and its devicetree node name without the leading
   * "fx_" (static storage; NULL when the effect has no device). */
  uint8_t effect;
  uint8_t effect_count;
  const char *effect_name;
  /* The active effect's colour: hue 0..359, saturation 0..100. */
  uint16_t hue;
  uint8_t saturation;
  /* The controller's brightness, 0..100. */
  uint8_t brightness;
  /* The active effect's animation period in milliseconds: a shorter period is a
   * faster animation. */
  uint16_t period_ms;
};

ZMK_EVENT_DECLARE(zmk_rgb_matrix_changed);

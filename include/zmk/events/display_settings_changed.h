/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>

/* Raised (CONFIG_KEYPAW_OLED_LAYOUT) after a setting of the OLED layout changed:
 * edited in ZMK Studio, received from the central by a peripheral, or put back
 * to its default. Also raised once after the saved settings are loaded at boot,
 * with id 0, because a widget that drew before that drew with the defaults.
 *
 * Changes that follow one another within a few milliseconds make one event, so
 * the widgets draw again once: the event does not say which setting changed,
 * only the last of them. Read the values with kp_display_setting_get()
 * (zmk/display_settings.h).
 *
 * Listeners run in the thread of ZMK's event manager. Keep them short: hand the
 * drawing to the display work queue (zmk_display_work_q()), the way the widgets
 * of zmk-nice-oled do. */
struct zmk_display_settings_changed {
  /* The id of the last setting of the burst (enum kp_display_setting_id), or 0
   * after the load at boot. For the log: do not depend on it. */
  uint8_t id;
};

ZMK_EVENT_DECLARE(zmk_display_settings_changed);

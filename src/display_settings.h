/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Internal interface of the display settings (display_settings.c): what the
 * ZMK Studio handlers, the split sync and the RGB behavior use. The public part
 * (the ids and kp_display_setting_get) is include/zmk/display_settings.h.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/keypaw/rgb_matrix.h>
#include <zmk/display_settings.h>

/* Wire format of RGB_DISPLAY_SET_CMD (dt-bindings/keypaw/rgb_matrix.h).
 * param1 = command | setting id << 12, so a setting id is 0 to 255 (bits 12 to
 * 19); param2 is the value, an int32 carried as a uint32. */
#define RGB_DISPLAY_CMD_MASK 0x0FFFu
#define RGB_DISPLAY_ID_SHIFT 12
#define RGB_DISPLAY_PARAM1(cmd, id)                                            \
  ((uint32_t)(cmd) | ((uint32_t)(id) << RGB_DISPLAY_ID_SHIFT))
#define RGB_DISPLAY_PARAM1_CMD(param1) ((param1) & RGB_DISPLAY_CMD_MASK)
#define RGB_DISPLAY_PARAM1_ID(param1)                                          \
  ((uint8_t)(((param1) >> RGB_DISPLAY_ID_SHIFT) & 0xFFu))
#define RGB_DISPLAY_IS_CMD(param1)                                             \
  ((param1) <= 0xFFFFFu && RGB_DISPLAY_PARAM1_CMD(param1) == RGB_DISPLAY_SET_CMD)

/* One setting as ZMK Studio reports it. */
struct kp_display_setting_info {
  uint8_t id;
  int32_t value;
  int32_t min;
  int32_t max;
  int32_t default_value;
};

/* The number of settings; the entries are indexed 0 to count - 1. */
size_t kp_display_setting_count(void);
/* Fills `out` for entry `index`; -EINVAL past the end. */
int kp_display_setting_info(size_t index, struct kp_display_setting_info *out);

/* A change asked for by the user (ZMK Studio): applies it at once, keeps it in
 * flash and, on the central, queues it for the peripheral. -ENOENT for an id
 * that does not exist, -EINVAL for a value outside its range. */
int kp_display_setting_set(uint8_t id, int32_t value);
/* Puts every setting back to its default, the same way. */
int kp_display_settings_restore_defaults(void);
/* ZMK Studio's "restore stock settings": the defaults, and the flash records
 * deleted. */
int kp_display_settings_settings_reset(void);

/* RGB_DISPLAY_SET_CMD, as a peripheral receives it from the central. */
int kp_display_settings_command(uint32_t param1, uint32_t param2);

/* ---- split sync, on the central ------------------------------------------- */

/* What the central keeps for each peripheral, per entry of the table, to send
 * only what the peripheral does not hold: the value last sent to it, or this
 * after a (re)connection, when nothing is known. */
#define KP_DISPLAY_SYNC_UNKNOWN INT32_MIN

/* The index of the first entry, from `from` on, that a peripheral applies (the
 * table says which) and that `shadow` (KP_DISPLAY_SETTING_CAP entries) does not
 * show it holding; -1 when there is none. */
int kp_display_sync_next(size_t from, const int32_t *shadow);
/* Entry `index` as the command that sets it on a peripheral, with the value it
 * has now; false past the end. */
bool kp_display_sync_command(size_t index, uint32_t *param1, uint32_t *param2);

/* Asks the split sync to push the settings again: any context, no-op until the
 * sync has started and on a build without it. */
#if IS_ENABLED(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
void kp_rgb_sync_display_kick(void);
#else
static inline void kp_rgb_sync_display_kick(void) {}
#endif

/* Asks the split sync to tell the peripheral that the keyboard is being used
 * (`key`: a key was pressed on the central just now, which at most one report
 * every few seconds carries) and whether the central has USB power. Without `key`
 * it is only the USB state. Any context; no-op on a peripheral, and on a build
 * without the split sync (idle_policy.c). */
#if IS_ENABLED(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
void kp_rgb_sync_activity_kick(bool key);
#else
static inline void kp_rgb_sync_activity_kick(bool key) { ARG_UNUSED(key); }
#endif

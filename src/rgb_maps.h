/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Editable per-layer colour maps.
 *
 * A map is a `static` effect instance that declares `layer-id` (and
 * `slot-colors`). Its colours are kept by slot, in a slot space that is the same
 * on both halves of a split keyboard (see `slot-count` and `no-key-slots` in the
 * keypaw,rgb-matrix binding), so an edit made on the central can be sent to the
 * peripheral by slot.
 *
 * Every map keeps three copies of its colours:
 *   defaults  the devicetree `slot-colors`, in ROM
 *   live      what is shown now; edits change this one
 *   saved     what was last written to flash or loaded from it (the central only
 *             writes flash); a discard returns `live` to it
 * The records on flash hold only the slots whose colour differs from `defaults`.
 *
 * Thread safety: every function here takes the matrix lock itself unless said
 * otherwise, and none holds it across flash I/O.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include <dt-bindings/keypaw/rgb_matrix.h>
#include <zmk/rgb_matrix.h>

#include "rgb_map_codec.h"

/* Entries in a map's colour arrays: KP_RGB_SLOT_COUNT, and never zero so that the
 * declarations stay valid when the slot model is off. */
#if KP_RGB_SLOT_COUNT > 0
#define KP_RGB_SLOT_CAP KP_RGB_SLOT_COUNT
#else
#define KP_RGB_SLOT_CAP 1
#endif

/* The number of editable maps: the `static` effect instances that declare
 * `layer-id`. */
#define KP_RGB_MAP_NODE_ONE(node_id)                                           \
  COND_CODE_1(DT_NODE_HAS_PROP(node_id, layer_id), (+1), ())
#if DT_HAS_COMPAT_STATUS_OKAY(keypaw_rgb_matrix_static)
#define KP_RGB_MAP_COUNT                                                       \
  (0 DT_FOREACH_STATUS_OKAY(keypaw_rgb_matrix_static, KP_RGB_MAP_NODE_ONE))
#else
#define KP_RGB_MAP_COUNT 0
#endif

/* Wire format of RGB_MAP_SET_CMD and RGB_MAP_RESET_CMD
 * (dt-bindings/keypaw/rgb_matrix.h). param1 = command | layer id << 12,
 * so a layer id is 0 to 14 and 0xF means "every map" in a reset. */
#define RGB_MAP_CMD_MASK 0x0FFFu
#define RGB_MAP_LAYER_SHIFT 12
#define RGB_MAP_ALL 0xFu
#define RGB_MAP_PARAM1(cmd, layer)                                             \
  ((uint32_t)(cmd) | ((uint32_t)(layer) << RGB_MAP_LAYER_SHIFT))
#define RGB_MAP_PARAM1_CMD(param1) ((param1) & RGB_MAP_CMD_MASK)
#define RGB_MAP_PARAM1_LAYER(param1)                                           \
  ((uint8_t)(((param1) >> RGB_MAP_LAYER_SHIFT) & 0xFu))
#define RGB_MAP_IS_CMD(param1)                                                 \
  ((param1) <= 0xFFFFu && (RGB_MAP_PARAM1_CMD(param1) == RGB_MAP_SET_CMD ||    \
                           RGB_MAP_PARAM1_CMD(param1) == RGB_MAP_RESET_CMD))
/* param2 of RGB_MAP_SET_CMD: the slot and its 0xRRGGBB colour. */
#define RGB_MAP_SET_VAL(slot, rgb)                                             \
  (((uint32_t)(slot) << 24) | ((uint32_t)(rgb) & 0xFFFFFFu))
#define RGB_MAP_SET_SLOT(param2) ((uint8_t)((param2) >> 24))
#define RGB_MAP_SET_RGB(param2) ((param2) & 0xFFFFFFu)

/* One slot of one map, as set by an edit. `rgb` is 0xRRGGBB. */
struct kp_rgb_map_edit {
  uint8_t slot;
  uint32_t rgb;
};

/* The three colour arrays of a map's `static` effect instance (effects/static.c),
 * KP_RGB_SLOT_CAP entries each. The caller holds the matrix lock to touch the
 * writable ones; defaults is read-only. */
uint32_t *kp_eff_static_live(const struct device *dev);
uint32_t *kp_eff_static_saved(const struct device *dev);
const uint32_t *kp_eff_static_defaults(const struct device *dev);

/* ---- registry (rgb_maps.c) ------------------------------------------------ */

size_t kp_rgb_map_count(void);
/* Maps are indexed in devicetree order, which is the same on both halves. */
uint8_t kp_rgb_map_layer_id(size_t index);
/* The index of the map of a layer, or -ENOENT. */
int kp_rgb_map_find(uint8_t layer_id);

/* ---- live edits: any thread ------------------------------------------------ */

/* Copies a map's live colours: KP_RGB_SLOT_COUNT entries. -ENOENT when the layer
 * has no map. */
int kp_rgb_map_read(uint8_t layer_id, uint32_t *colors);
/* Applies every edit or none: -ENOENT for a layer without a map, -EINVAL for a
 * slot outside the slot space. A real change repaints at once and is queued for
 * the peripheral. */
int kp_rgb_map_edit(uint8_t layer_id, const struct kp_rgb_map_edit *edits,
                    size_t count);
/* Puts one map's live colours back to the devicetree defaults. */
int kp_rgb_map_restore_defaults(uint8_t layer_id);
/* Same for every map. */
void kp_rgb_maps_restore_all_defaults(void);
/* Runs RGB_MAP_SET_CMD or RGB_MAP_RESET_CMD (what a peripheral receives). */
int kp_rgb_map_command(uint32_t param1, uint32_t param2);

/* ---- saving and discarding: the central only ------------------------------ */

/* True when some map's live colours differ from its saved ones. */
bool kp_rgb_maps_unsaved(void);
/* Writes every map that differs from its saved state to flash (a map with no
 * edit left loses its record). Returns the last error, 0 when all succeeded. */
int kp_rgb_maps_save(void);
/* Puts every map's live colours back to its saved state. */
int kp_rgb_maps_discard(void);
/* Deletes every map record and puts live and saved back to the defaults. This
 * is what RGB_RESET and ZMK Studio's "restore stock settings" do. */
int kp_rgb_maps_settings_reset(void);
/* Settings load callback for the keys under "keypaw/rgb_matrix/map/"; `name` is
 * what follows that prefix, the decimal layer id. */
int kp_rgb_maps_load(const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg);

/* ---- split sync, on the central ------------------------------------------- */

/* Copies the defaults of every map, in map order, into an array of
 * KP_RGB_MAP_COUNT * KP_RGB_SLOT_CAP colours. */
void kp_rgb_maps_fill_defaults(uint32_t *colors);
/* Finds the first slot that no local LED paints (so the peripheral does) whose
 * live colour differs from `shadow`, an array laid out like the one above. */
bool kp_rgb_maps_next_peer_diff(const uint32_t *shadow, size_t *map,
                                size_t *slot, uint32_t *rgb);

/* Asks the split sync to push the maps again: any context, no-op until the sync
 * has started and on a build without it. */
#if IS_ENABLED(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
void kp_rgb_sync_maps_kick(void);
#else
static inline void kp_rgb_sync_maps_kick(void) {}
#endif

/* Tells a connected ZMK Studio client that the unsaved state changed
 * (studio_rpc.c). */
#if IS_ENABLED(CONFIG_KEYPAW_RGB_MAPS_STUDIO)
void kp_rgb_studio_notify_unsaved(bool unsaved);
#else
static inline void kp_rgb_studio_notify_unsaved(bool unsaved) {
  ARG_UNUSED(unsaved);
}
#endif

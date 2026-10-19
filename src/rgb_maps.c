/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Editable per-layer colour maps: registry, live edits, save, discard and the
 * flash records. See rgb_maps.h for the model.
 */

#define DT_DRV_COMPAT keypaw_rgb_matrix_static

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/rgb_matrix.h>

#include "rgb_map_codec.h"
#include "rgb_matrix_internal.h"
#include "rgb_maps.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if KP_RGB_MAP_COUNT > 0

BUILD_ASSERT(KP_RGB_SLOT_COUNT <= KP_RGB_SLOT_MAX,
             "keypaw,rgb-matrix: slot-count is limited to KP_RGB_SLOT_MAX");
#if IS_ENABLED(CONFIG_SETTINGS)
BUILD_ASSERT(KP_RGB_MAP_RECORD_MAX_LEN <= SETTINGS_MAX_VAL_LEN,
             "the persisted map record exceeds SETTINGS_MAX_VAL_LEN");
#endif

struct kp_rgb_map_entry {
  const struct device *dev;
  uint8_t layer_id;
};

#define KP_RGB_MAP_ENTRY(inst)                                                 \
  COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, layer_id),                           \
              ({.dev = DEVICE_DT_INST_GET(inst),                               \
                .layer_id = DT_INST_PROP(inst, layer_id)},),                   \
              ())

/* Devicetree order, so the map indices mean the same on both halves. */
static const struct kp_rgb_map_entry kp_rgb_maps[KP_RGB_MAP_COUNT] = {
    DT_INST_FOREACH_STATUS_OKAY(KP_RGB_MAP_ENTRY)};

#define KP_RGB_MAP_ASSERT(inst)                                                \
  COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, layer_id),                           \
              (BUILD_ASSERT(DT_INST_PROP(inst, layer_id) < RGB_MAP_ALL,        \
                            "layer-id must be 0 to 14");                       \
               BUILD_ASSERT(DT_INST_NODE_HAS_PROP(inst, slot_colors),          \
                            "layer-id needs slot-colors");),                   \
              ())
DT_INST_FOREACH_STATUS_OKAY(KP_RGB_MAP_ASSERT)

#define KP_RGB_MAP_BYTES (KP_RGB_SLOT_CAP * sizeof(uint32_t))
#define KP_RGB_MAP_COLOR_MASK 0xFFFFFFu

static const struct kp_rgb_map_entry *kp_rgb_map_entry_for(uint8_t layer_id) {
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    if (kp_rgb_maps[i].layer_id == layer_id) {
      return &kp_rgb_maps[i];
    }
  }
  return NULL;
}

size_t kp_rgb_map_count(void) { return KP_RGB_MAP_COUNT; }

uint8_t kp_rgb_map_layer_id(size_t index) {
  return index < KP_RGB_MAP_COUNT ? kp_rgb_maps[index].layer_id : 0;
}

int kp_rgb_map_find(uint8_t layer_id) {
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    if (kp_rgb_maps[i].layer_id == layer_id) {
      return (int)i;
    }
  }
  return -ENOENT;
}

bool kp_rgb_maps_unsaved(void) {
  bool unsaved = false;

  kp_rgb_matrix_lock();
  for (size_t i = 0; i < KP_RGB_MAP_COUNT && !unsaved; i++) {
    unsaved = memcmp(kp_eff_static_live(kp_rgb_maps[i].dev),
                     kp_eff_static_saved(kp_rgb_maps[i].dev),
                     KP_RGB_MAP_BYTES) != 0;
  }
  kp_rgb_matrix_unlock();
  return unsaved;
}

/* What the Studio client was last told, so a notification goes out only when the
 * answer changes. */
static atomic_t kp_rgb_maps_unsaved_reported;

static void kp_rgb_maps_note_unsaved(void) {
  bool unsaved = kp_rgb_maps_unsaved();

  if ((bool)atomic_set(&kp_rgb_maps_unsaved_reported, unsaved) != unsaved) {
    kp_rgb_studio_notify_unsaved(unsaved);
  }
}

/* After a live change, with the matrix lock released: repaint, send the change
 * to the peripheral and report the unsaved state. */
static void kp_rgb_maps_changed(const struct device *dev) {
  kp_rgb_effect_invalidate(dev);
  kp_rgb_sync_maps_kick();
  kp_rgb_maps_note_unsaved();
}

int kp_rgb_map_read(uint8_t layer_id, uint32_t *colors) {
  const struct kp_rgb_map_entry *map = kp_rgb_map_entry_for(layer_id);

  if (map == NULL) {
    return -ENOENT;
  }
  kp_rgb_matrix_lock();
  memcpy(colors, kp_eff_static_live(map->dev),
         KP_RGB_SLOT_COUNT * sizeof(uint32_t));
  kp_rgb_matrix_unlock();
  return 0;
}

int kp_rgb_map_edit(uint8_t layer_id, const struct kp_rgb_map_edit *edits,
                    size_t count) {
  const struct kp_rgb_map_entry *map = kp_rgb_map_entry_for(layer_id);

  if (map == NULL) {
    return -ENOENT;
  }
  for (size_t i = 0; i < count; i++) {
    if (edits[i].slot >= KP_RGB_SLOT_COUNT) {
      return -EINVAL;
    }
  }

  uint32_t *live = kp_eff_static_live(map->dev);
  bool changed = false;

  kp_rgb_matrix_lock();
  for (size_t i = 0; i < count; i++) {
    uint32_t rgb = edits[i].rgb & KP_RGB_MAP_COLOR_MASK;
    if (live[edits[i].slot] != rgb) {
      live[edits[i].slot] = rgb;
      changed = true;
    }
  }
  kp_rgb_matrix_unlock();

  if (changed) {
    kp_rgb_maps_changed(map->dev);
  }
  return 0;
}

int kp_rgb_map_restore_defaults(uint8_t layer_id) {
  const struct kp_rgb_map_entry *map = kp_rgb_map_entry_for(layer_id);

  if (map == NULL) {
    return -ENOENT;
  }

  uint32_t *live = kp_eff_static_live(map->dev);
  const uint32_t *defaults = kp_eff_static_defaults(map->dev);
  bool changed;

  kp_rgb_matrix_lock();
  changed = memcmp(live, defaults, KP_RGB_MAP_BYTES) != 0;
  memcpy(live, defaults, KP_RGB_MAP_BYTES);
  kp_rgb_matrix_unlock();

  if (changed) {
    kp_rgb_maps_changed(map->dev);
  }
  return 0;
}

void kp_rgb_maps_restore_all_defaults(void) {
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    (void)kp_rgb_map_restore_defaults(kp_rgb_maps[i].layer_id);
  }
}

int kp_rgb_map_command(uint32_t param1, uint32_t param2) {
  uint8_t layer = RGB_MAP_PARAM1_LAYER(param1);

  switch (RGB_MAP_PARAM1_CMD(param1)) {
  case RGB_MAP_SET_CMD: {
    const struct kp_rgb_map_edit edit = {
        .slot = RGB_MAP_SET_SLOT(param2),
        .rgb = RGB_MAP_SET_RGB(param2),
    };
    return kp_rgb_map_edit(layer, &edit, 1);
  }
  case RGB_MAP_RESET_CMD:
    if (layer == RGB_MAP_ALL) {
      kp_rgb_maps_restore_all_defaults();
      return 0;
    }
    return kp_rgb_map_restore_defaults(layer);
  default:
    return -ENOTSUP;
  }
}

int kp_rgb_maps_discard(void) {
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    const struct device *dev = kp_rgb_maps[i].dev;
    uint32_t *live = kp_eff_static_live(dev);
    const uint32_t *saved = kp_eff_static_saved(dev);
    bool changed;

    kp_rgb_matrix_lock();
    changed = memcmp(live, saved, KP_RGB_MAP_BYTES) != 0;
    memcpy(live, saved, KP_RGB_MAP_BYTES);
    kp_rgb_matrix_unlock();

    if (changed) {
      kp_rgb_maps_changed(dev);
    }
  }
  return 0;
}

void kp_rgb_maps_fill_defaults(uint32_t *colors) {
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    memcpy(&colors[i * KP_RGB_SLOT_CAP], kp_eff_static_defaults(kp_rgb_maps[i].dev),
           KP_RGB_MAP_BYTES);
  }
}

bool kp_rgb_maps_next_peer_diff(const uint32_t *shadow, size_t *map,
                                size_t *slot, uint32_t *rgb) {
  bool found = false;

  kp_rgb_matrix_lock();
  for (size_t m = 0; m < KP_RGB_MAP_COUNT && !found; m++) {
    const uint32_t *live = kp_eff_static_live(kp_rgb_maps[m].dev);
    for (size_t s = 0; s < KP_RGB_SLOT_COUNT; s++) {
      /* A slot that a local LED paints is this half's own business. */
      if (kp_rgb_led_for_slot(s) != SIZE_MAX ||
          live[s] == shadow[m * KP_RGB_SLOT_CAP + s]) {
        continue;
      }
      *map = m;
      *slot = s;
      *rgb = live[s];
      found = true;
      break;
    }
  }
  kp_rgb_matrix_unlock();
  return found;
}

#if IS_ENABLED(CONFIG_SETTINGS)

#define KP_RGB_MAPS_PATH "keypaw/rgb_matrix/map/"
/* The layer id has at most two digits (0 to 14). */
#define KP_RGB_MAPS_PATH_LEN (sizeof(KP_RGB_MAPS_PATH) + 2)

BUILD_ASSERT(KP_RGB_MAPS_PATH_LEN <= SETTINGS_MAX_NAME_LEN,
             "the persisted map settings key exceeds SETTINGS_MAX_NAME_LEN");

static int kp_rgb_map_path(char *out, size_t out_len, uint8_t layer_id) {
  int len = snprintk(out, out_len, KP_RGB_MAPS_PATH "%u", (unsigned int)layer_id);

  return len < 0 || (size_t)len >= out_len ? -ENAMETOOLONG : 0;
}

/* Writes the colours as the map's record, or deletes the record when no slot
 * differs from the defaults any more. Takes a private copy of the colours: no
 * lock is held here. */
static int kp_rgb_map_persist(const struct kp_rgb_map_entry *map,
                              const uint32_t *colors) {
  uint8_t record[KP_RGB_MAP_RECORD_MAX_LEN];
  char path[KP_RGB_MAPS_PATH_LEN];
  int err = kp_rgb_map_path(path, sizeof(path), map->layer_id);

  if (err < 0) {
    return err;
  }

  size_t len = kp_rgb_map_record_encode(colors, kp_eff_static_defaults(map->dev),
                                        KP_RGB_SLOT_COUNT, record);
  if (len == KP_RGB_MAP_RECORD_HEADER_LEN) {
    err = settings_delete(path);
    return err == -ENOENT ? 0 : err;
  }
  return settings_save_one(path, record, len);
}

int kp_rgb_maps_save(void) {
  int ret = 0;

  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    const struct kp_rgb_map_entry *map = &kp_rgb_maps[i];
    uint32_t snapshot[KP_RGB_SLOT_CAP];
    bool dirty;

    kp_rgb_matrix_lock();
    memcpy(snapshot, kp_eff_static_live(map->dev), sizeof(snapshot));
    dirty = memcmp(snapshot, kp_eff_static_saved(map->dev), sizeof(snapshot)) != 0;
    kp_rgb_matrix_unlock();

    if (!dirty) {
      continue;
    }

    int err = kp_rgb_map_persist(map, snapshot);
    if (err < 0) {
      LOG_WRN("Failed to save the colour map of layer %u (err %d)",
              (unsigned int)map->layer_id, err);
      ret = err;
      continue;
    }

    /* What was written, not the live colours: an edit that arrived while the
     * flash was busy is still unsaved. */
    kp_rgb_matrix_lock();
    memcpy(kp_eff_static_saved(map->dev), snapshot, sizeof(snapshot));
    kp_rgb_matrix_unlock();
  }

  kp_rgb_maps_note_unsaved();
  return ret;
}

int kp_rgb_maps_settings_reset(void) {
  int ret = 0;

  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    char path[KP_RGB_MAPS_PATH_LEN];
    int err = kp_rgb_map_path(path, sizeof(path), kp_rgb_maps[i].layer_id);

    if (err == 0) {
      err = settings_delete(path);
    }
    if (err < 0 && err != -ENOENT) {
      LOG_WRN("Failed to clear the colour map of layer %u (err %d)",
              (unsigned int)kp_rgb_maps[i].layer_id, err);
      ret = err;
    }
  }

  /* RAM goes back to the defaults even if a record could not be deleted. */
  for (size_t i = 0; i < KP_RGB_MAP_COUNT; i++) {
    const struct device *dev = kp_rgb_maps[i].dev;
    uint32_t *live = kp_eff_static_live(dev);
    uint32_t *saved = kp_eff_static_saved(dev);
    const uint32_t *defaults = kp_eff_static_defaults(dev);
    bool changed;

    kp_rgb_matrix_lock();
    changed = memcmp(live, defaults, KP_RGB_MAP_BYTES) != 0;
    memcpy(live, defaults, KP_RGB_MAP_BYTES);
    memcpy(saved, defaults, KP_RGB_MAP_BYTES);
    kp_rgb_matrix_unlock();

    if (changed) {
      kp_rgb_maps_changed(dev);
    }
  }

  kp_rgb_maps_note_unsaved();
  return ret;
}

int kp_rgb_maps_load(const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg) {
  /* `name` is the decimal layer id. */
  unsigned int layer_id = 0;
  size_t digits = 0;

  for (; name[digits] >= '0' && name[digits] <= '9' && digits < 3; digits++) {
    layer_id = layer_id * 10 + (unsigned int)(name[digits] - '0');
  }
  if (digits == 0 || name[digits] != '\0' || layer_id >= RGB_MAP_ALL ||
      len > KP_RGB_MAP_RECORD_MAX_LEN) {
    return -EINVAL;
  }

  uint8_t record[KP_RGB_MAP_RECORD_MAX_LEN];
  int rc = read_cb(cb_arg, record, len);

  if (rc < 0) {
    return rc;
  }
  if ((size_t)rc != len) {
    return -EINVAL;
  }

  const struct kp_rgb_map_entry *map = kp_rgb_map_entry_for((uint8_t)layer_id);
  if (map == NULL) {
    /* A record of a layer that no longer has a map: nothing to apply. */
    return 0;
  }

  uint32_t colors[KP_RGB_SLOT_CAP];
  memcpy(colors, kp_eff_static_defaults(map->dev), sizeof(colors));
  int err = kp_rgb_map_record_decode(record, len, KP_RGB_SLOT_COUNT, colors);
  if (err < 0) {
    LOG_WRN("Ignoring a damaged colour map record for layer %u", layer_id);
    return err;
  }

  kp_rgb_matrix_lock();
  memcpy(kp_eff_static_live(map->dev), colors, sizeof(colors));
  memcpy(kp_eff_static_saved(map->dev), colors, sizeof(colors));
  zmk_rgb_matrix_flush();
  kp_rgb_matrix_unlock();
  return 0;
}

#else /* !CONFIG_SETTINGS: nothing to persist */

int kp_rgb_maps_save(void) { return -ENOTSUP; }

int kp_rgb_maps_settings_reset(void) {
  kp_rgb_maps_restore_all_defaults();
  return 0;
}

int kp_rgb_maps_load(const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg) {
  ARG_UNUSED(name);
  ARG_UNUSED(len);
  ARG_UNUSED(read_cb);
  ARG_UNUSED(cb_arg);
  return -ENOTSUP;
}

#endif /* CONFIG_SETTINGS */

#else /* KP_RGB_MAP_COUNT == 0: no map is editable */

size_t kp_rgb_map_count(void) { return 0; }
uint8_t kp_rgb_map_layer_id(size_t index) {
  ARG_UNUSED(index);
  return 0;
}
int kp_rgb_map_find(uint8_t layer_id) {
  ARG_UNUSED(layer_id);
  return -ENOENT;
}
int kp_rgb_map_read(uint8_t layer_id, uint32_t *colors) {
  ARG_UNUSED(layer_id);
  ARG_UNUSED(colors);
  return -ENOENT;
}
int kp_rgb_map_edit(uint8_t layer_id, const struct kp_rgb_map_edit *edits,
                    size_t count) {
  ARG_UNUSED(layer_id);
  ARG_UNUSED(edits);
  ARG_UNUSED(count);
  return -ENOENT;
}
int kp_rgb_map_restore_defaults(uint8_t layer_id) {
  ARG_UNUSED(layer_id);
  return -ENOENT;
}
void kp_rgb_maps_restore_all_defaults(void) {}
int kp_rgb_map_command(uint32_t param1, uint32_t param2) {
  ARG_UNUSED(param1);
  ARG_UNUSED(param2);
  return -ENOENT;
}
bool kp_rgb_maps_unsaved(void) { return false; }
int kp_rgb_maps_save(void) { return 0; }
int kp_rgb_maps_discard(void) { return 0; }
int kp_rgb_maps_settings_reset(void) { return 0; }
int kp_rgb_maps_load(const char *name, size_t len, settings_read_cb read_cb,
                     void *cb_arg) {
  ARG_UNUSED(name);
  ARG_UNUSED(len);
  ARG_UNUSED(read_cb);
  ARG_UNUSED(cb_arg);
  return 0;
}
void kp_rgb_maps_fill_defaults(uint32_t *colors) { ARG_UNUSED(colors); }
bool kp_rgb_maps_next_peer_diff(const uint32_t *shadow, size_t *map,
                                size_t *slot, uint32_t *rgb) {
  ARG_UNUSED(shadow);
  ARG_UNUSED(map);
  ARG_UNUSED(slot);
  ARG_UNUSED(rgb);
  return false;
}

#endif /* KP_RGB_MAP_COUNT */

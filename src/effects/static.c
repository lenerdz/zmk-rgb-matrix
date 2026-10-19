/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * "static" effect: paints each LED with a user-supplied colour. This is a strict
 * superset of QMK's ALPHAS_MODS (assign a different colour to alpha vs mod keys)
 * and any other static per-key art.
 *
 * The colours come from one of two lists:
 *   - `slot-colors`, indexed by colour slot. A slot means the same thing on both
 *     halves of a split keyboard (the slot of a key is its position), so the same
 *     list serves both. With `layer-id` the map is also editable at run time
 *     (rgb_maps.c): the colours then live in RAM, and the list is the default.
 *   - `led-colors`, indexed by LED chain index. Used only when `slot-colors` is
 *     absent.
 */

#define DT_DRV_COMPAT keypaw_rgb_matrix_static

#include <stdint.h>

#include <zephyr/sys/util.h>

#include <zmk/rgb_matrix.h>

#include "../rgb_maps.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct kp_eff_static_config {
  struct kp_rgb_effect_common_config common;
  const uint32_t *colors; /* indexed by LED chain index */
  size_t colors_len;
  /* Indexed by slot, KP_RGB_SLOT_CAP entries; NULL without `slot-colors`. */
  const uint32_t *slot_colors;
};

struct kp_eff_static_data {
  struct kp_rgb_effect_common_data common;
  /* By slot. Read by render under the matrix lock; edits take the same lock. */
  uint32_t live[KP_RGB_SLOT_CAP];  /* shown now */
  uint32_t saved[KP_RGB_SLOT_CAP]; /* last saved or loaded: what a discard restores */
};

#define KP_EFF_STATIC_COLOR(idx, node) DT_PROP_BY_IDX(node, led_colors, idx),

/* The `slot-colors` list as an array initializer; the unlisted slots are 0. */
#define KP_EFF_STATIC_SLOTS(inst)                                              \
  COND_CODE_1(DT_NODE_HAS_PROP(DT_DRV_INST(inst), slot_colors),                \
              (DT_PROP(DT_DRV_INST(inst), slot_colors)), ({0}))

#define KP_EFF_STATIC_DEFINE(inst)                                             \
  COND_CODE_1(                                                                 \
      DT_NODE_HAS_PROP(DT_DRV_INST(inst), led_colors),                         \
      (static const uint32_t kp_eff_static_##inst##_colors[] = {LISTIFY(       \
           DT_PROP_LEN(DT_DRV_INST(inst), led_colors), KP_EFF_STATIC_COLOR,    \
           (), DT_DRV_INST(inst))};),                                          \
      (static const uint32_t kp_eff_static_##inst##_colors[] = {0};))          \
  BUILD_ASSERT(KP_LED_COUNT == 0 ||                                            \
                   ARRAY_SIZE(kp_eff_static_##inst##_colors) <= KP_LED_COUNT,  \
               "led-colors has more entries than LEDs");                       \
  BUILD_ASSERT(DT_PROP_LEN_OR(DT_DRV_INST(inst), slot_colors, 0) <=            \
                   KP_RGB_SLOT_COUNT,                                          \
               "slot-colors has more entries than the matrix has slots "       \
               "(set slot-count on the keypaw,rgb-matrix node)");              \
  COND_CODE_1(DT_NODE_HAS_PROP(DT_DRV_INST(inst), slot_colors),                \
              (static const uint32_t kp_eff_static_##inst##_slot_colors        \
                   [KP_RGB_SLOT_CAP] = KP_EFF_STATIC_SLOTS(inst);),            \
              ())                                                              \
  static const struct kp_eff_static_config kp_eff_static_##inst##_cfg = {      \
      KP_RGB_EFFECT_COMMON_CONFIG(inst),                                       \
      .colors = kp_eff_static_##inst##_colors,                                 \
      .colors_len = ARRAY_SIZE(kp_eff_static_##inst##_colors),                 \
      .slot_colors = COND_CODE_1(                                              \
          DT_NODE_HAS_PROP(DT_DRV_INST(inst), slot_colors),                    \
          (kp_eff_static_##inst##_slot_colors), (NULL)),                       \
  };                                                                           \
  static struct kp_eff_static_data kp_eff_static_##inst##_data = {             \
      KP_RGB_EFFECT_COMMON_DATA(inst, 0xFFFFFF, 0),                            \
      .live = KP_EFF_STATIC_SLOTS(inst),                                       \
      .saved = KP_EFF_STATIC_SLOTS(inst),                                      \
  };                                                                           \
  KP_RGB_EFFECT_DEFINE(DT_DRV_INST(inst), &kp_eff_static_callbacks,            \
                       kp_eff_static_##inst)

static bool kp_eff_static_render(const struct device *dev, const struct kp_rgb_frame *f) {
  const struct kp_eff_static_config *cfg = dev->config;
  const struct kp_eff_static_data *data = dev->data;

  for (size_t t = 0; t < f->target_count; t++) {
    size_t i = f->targets[t];
    uint32_t hex;

    if (cfg->slot_colors != NULL) {
      size_t slot = kp_rgb_slot_for_led(i);
      hex = (slot < KP_RGB_SLOT_COUNT) ? data->live[slot] : 0;
    } else {
      hex = (i < cfg->colors_len) ? cfg->colors[i] : 0;
    }
    struct led_rgb rgb = kp_hex_to_rgb(hex);
    f->pixels[i] = rgb;
  }
  return false;
}

static const struct kp_rgb_effect_callbacks kp_eff_static_callbacks = {
    .render = kp_eff_static_render,
};

uint32_t *kp_eff_static_live(const struct device *dev) {
  struct kp_eff_static_data *data = dev->data;
  return data->live;
}

uint32_t *kp_eff_static_saved(const struct device *dev) {
  struct kp_eff_static_data *data = dev->data;
  return data->saved;
}

const uint32_t *kp_eff_static_defaults(const struct device *dev) {
  const struct kp_eff_static_config *cfg = dev->config;
  return cfg->slot_colors;
}

DT_INST_FOREACH_STATUS_OKAY(KP_EFF_STATIC_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>

#include "rgb_map_codec.h"

#define KP_RGB_MAP_RGB_MASK 0xFFFFFFu

size_t kp_rgb_map_record_encode(const uint32_t *colors, const uint32_t *defaults,
                                size_t slot_count, uint8_t *out) {
  size_t count = 0;
  uint8_t *entry = out + KP_RGB_MAP_RECORD_HEADER_LEN;

  for (size_t slot = 0; slot < slot_count && slot < KP_RGB_SLOT_MAX; slot++) {
    uint32_t color = colors[slot] & KP_RGB_MAP_RGB_MASK;
    if (color == (defaults[slot] & KP_RGB_MAP_RGB_MASK)) {
      continue;
    }
    entry[0] = (uint8_t)slot;
    entry[1] = (uint8_t)(color >> 16);
    entry[2] = (uint8_t)(color >> 8);
    entry[3] = (uint8_t)color;
    entry += KP_RGB_MAP_RECORD_ENTRY_LEN;
    count++;
  }

  out[0] = KP_RGB_MAP_RECORD_VERSION;
  out[1] = (uint8_t)count;
  return KP_RGB_MAP_RECORD_HEADER_LEN + count * KP_RGB_MAP_RECORD_ENTRY_LEN;
}

int kp_rgb_map_record_decode(const uint8_t *record, size_t len, size_t slot_count,
                             uint32_t *colors) {
  if (record == NULL || colors == NULL || len < KP_RGB_MAP_RECORD_HEADER_LEN ||
      record[0] != KP_RGB_MAP_RECORD_VERSION || slot_count > KP_RGB_SLOT_MAX) {
    return -EINVAL;
  }

  size_t count = record[1];
  if (len != KP_RGB_MAP_RECORD_HEADER_LEN + count * KP_RGB_MAP_RECORD_ENTRY_LEN) {
    return -EINVAL;
  }

  /* Validate every entry before touching `colors`, so a damaged record changes
   * nothing. */
  uint64_t seen = 0;
  const uint8_t *entry = record + KP_RGB_MAP_RECORD_HEADER_LEN;
  for (size_t i = 0; i < count; i++, entry += KP_RGB_MAP_RECORD_ENTRY_LEN) {
    size_t slot = entry[0];
    if (slot >= slot_count || (seen & ((uint64_t)1 << slot)) != 0) {
      return -EINVAL;
    }
    seen |= (uint64_t)1 << slot;
  }

  entry = record + KP_RGB_MAP_RECORD_HEADER_LEN;
  for (size_t i = 0; i < count; i++, entry += KP_RGB_MAP_RECORD_ENTRY_LEN) {
    colors[entry[0]] = ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) |
                       (uint32_t)entry[3];
  }
  return 0;
}

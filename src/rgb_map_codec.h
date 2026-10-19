/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * On-flash record of one editable colour map. Pure: it needs nothing but the C
 * library, so the host unit tests compile it as it is.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Largest slot space. Slots are one byte on the wire and in the record, and a
 * record has to fit SETTINGS_MAX_VAL_LEN (256): 2 + 4 * 60 = 242 bytes. */
#define KP_RGB_SLOT_MAX 60

/* A record lists only the slots whose colour differs from the devicetree
 * default, so a map nobody edited stores nothing and a later change of the
 * defaults still reaches every slot that was never touched.
 *
 *   byte 0     version (KP_RGB_MAP_RECORD_VERSION)
 *   byte 1     entry count
 *   then count entries of 4 bytes: slot, red, green, blue
 *
 * Bump the version when this layout changes. */
#define KP_RGB_MAP_RECORD_VERSION 1
#define KP_RGB_MAP_RECORD_HEADER_LEN 2
#define KP_RGB_MAP_RECORD_ENTRY_LEN 4
#define KP_RGB_MAP_RECORD_MAX_LEN                                              \
  (KP_RGB_MAP_RECORD_HEADER_LEN + KP_RGB_MAP_RECORD_ENTRY_LEN * KP_RGB_SLOT_MAX)

/* Writes the record of `colors` against `defaults` (both slot_count entries,
 * slot_count <= KP_RGB_SLOT_MAX) into `out`, which must hold
 * KP_RGB_MAP_RECORD_MAX_LEN bytes, and returns its length. The result is just
 * the header when no slot differs. Only the low 24 bits of a colour count. */
size_t kp_rgb_map_record_encode(const uint32_t *colors, const uint32_t *defaults,
                                size_t slot_count, uint8_t *out);

/* Applies a record to `colors` (slot_count entries, normally pre-filled with
 * the defaults). Returns 0, or -EINVAL for a record that is too short, has the
 * wrong version or a length that disagrees with its count, names a slot at or
 * above slot_count, or names a slot twice; `colors` is then left untouched. */
int kp_rgb_map_record_decode(const uint8_t *record, size_t len, size_t slot_count,
                             uint32_t *colors);

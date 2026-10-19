/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK Studio access to the editable colour maps (rgb_maps.c).
 *
 * The requests extend the keymap subsystem of the zmk-studio-messages fork
 * (lenerdz/zmk-studio-messages): their request ids are 100 to 105, far from the
 * ones upstream uses. Every handler is secured, like the keymap ones: the
 * keyboard has to be unlocked.
 *
 * Central only: ZMK Studio runs there. The peripheral is kept up to date by the
 * split sync (rgb_split_sync.c).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/rgb_matrix.h>
#include <zmk/studio/rpc.h>

#include "rgb_maps.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEYMAP_RESPONSE(type, ...) ZMK_RPC_RESPONSE(keymap, type, __VA_ARGS__)

/* The wire arrays are fixed (keymap.options.in); the firmware must fit them. */
BUILD_ASSERT(KP_RGB_SLOT_COUNT * sizeof(uint32_t) <=
                 sizeof(((zmk_keymap_LedMap *)0)->colors),
             "slot-count does not fit the LedMap of the protocol: raise "
             "colors max_count in zmk-studio-messages (keymap.options.in)");
BUILD_ASSERT(KP_RGB_MAP_COUNT <= sizeof(((zmk_keymap_LedInfo *)0)->layer_ids) /
                                     sizeof(uint32_t),
             "more colour maps than the LedInfo of the protocol can list: "
             "raise layer_ids max_count in zmk-studio-messages");

/* ZMK Studio drops the bytes that do not fit its receive buffer, which holds 30
 * bytes unless the config says otherwise. A set_led_colors request with the 16
 * entries a client may send is 158 bytes once framed, and up to about 210 when
 * many of its bytes need an escape, so the request would arrive truncated and
 * the edit would be lost. Put CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=256 in the config
 * of the central half. */
BUILD_ASSERT(CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE >= 256,
             "ZMK Studio would drop the bytes of a set_led_colors request: set "
             "CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=256 (or more) on the central half");

static bool layer_id_valid(uint32_t layer_id) { return layer_id <= UINT8_MAX; }

static zmk_studio_Response get_led_info(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_LedInfo info = zmk_keymap_LedInfo_init_zero;

  info.slot_count = (uint32_t)kp_rgb_slot_count();
  info.layer_ids_count = (pb_size_t)kp_rgb_map_count();
  for (size_t i = 0; i < kp_rgb_map_count(); i++) {
    info.layer_ids[i] = kp_rgb_map_layer_id(i);
  }
  info.unsaved_changes = kp_rgb_maps_unsaved();

  return KEYMAP_RESPONSE(get_led_info, info);
}

static zmk_studio_Response get_led_map(const zmk_studio_Request *req) {
  uint32_t layer_id = req->subsystem.keymap.request_type.get_led_map;
  zmk_keymap_LedMap map = zmk_keymap_LedMap_init_zero;

  if (!layer_id_valid(layer_id) ||
      kp_rgb_map_read((uint8_t)layer_id, map.colors) < 0) {
    return ZMK_RPC_SIMPLE_ERR(GENERIC);
  }
  map.layer_id = layer_id;

  return KEYMAP_RESPONSE(get_led_map, map);
}

static zmk_studio_Response set_led_colors(const zmk_studio_Request *req) {
  const zmk_keymap_SetLedColorsRequest *set_req =
      &req->subsystem.keymap.request_type.set_led_colors;
  struct kp_rgb_map_edit edits[ARRAY_SIZE(set_req->entries)];
  zmk_keymap_SetLedColorsResponse resp =
      zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_OK;

  if (!layer_id_valid(set_req->layer_id)) {
    return KEYMAP_RESPONSE(
        set_led_colors,
        zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_INVALID_LAYER);
  }

  size_t count = MIN((size_t)set_req->entries_count, ARRAY_SIZE(edits));

  for (size_t i = 0; i < count; i++) {
    if (set_req->entries[i].slot > UINT8_MAX) {
      return KEYMAP_RESPONSE(
          set_led_colors,
          zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_INVALID_SLOT);
    }
    edits[i].slot = (uint8_t)set_req->entries[i].slot;
    edits[i].rgb = set_req->entries[i].rgb;
  }

  int ret = kp_rgb_map_edit((uint8_t)set_req->layer_id, edits, count);

  switch (ret) {
  case 0:
    break;
  case -ENOENT:
    resp = zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_INVALID_LAYER;
    break;
  case -EINVAL:
    resp = zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_INVALID_SLOT;
    break;
  default:
    LOG_WRN("Setting the colours failed with %d", ret);
    resp = zmk_keymap_SetLedColorsResponse_SET_LED_COLORS_RESP_BUSY;
    break;
  }

  return KEYMAP_RESPONSE(set_led_colors, resp);
}

static void map_errno_to_save_resp(int err, zmk_keymap_SaveChangesResponse *resp) {
  resp->which_result = zmk_keymap_SaveChangesResponse_err_tag;

  switch (err) {
  case -ENOTSUP:
    resp->result.err = zmk_keymap_SaveChangesErrorCode_SAVE_CHANGES_ERR_NOT_SUPPORTED;
    break;
  case -ENOSPC:
    resp->result.err = zmk_keymap_SaveChangesErrorCode_SAVE_CHANGES_ERR_NO_SPACE;
    break;
  default:
    resp->result.err = zmk_keymap_SaveChangesErrorCode_SAVE_CHANGES_ERR_GENERIC;
    break;
  }
}

static zmk_studio_Response save_led_changes(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_SaveChangesResponse resp = zmk_keymap_SaveChangesResponse_init_zero;
  int ret = kp_rgb_maps_save();

  if (ret < 0) {
    LOG_WRN("Failed to save the colour maps (%d)", ret);
    map_errno_to_save_resp(ret, &resp);
  } else {
    resp.which_result = zmk_keymap_SaveChangesResponse_ok_tag;
    resp.result.ok = true;
  }

  return KEYMAP_RESPONSE(save_led_changes, resp);
}

static zmk_studio_Response discard_led_changes(const zmk_studio_Request *req) {
  ARG_UNUSED(req);

  if (kp_rgb_maps_discard() < 0) {
    return ZMK_RPC_SIMPLE_ERR(GENERIC);
  }

  return KEYMAP_RESPONSE(discard_led_changes, true);
}

static zmk_studio_Response restore_led_defaults(const zmk_studio_Request *req) {
  uint32_t layer_id = req->subsystem.keymap.request_type.restore_led_defaults;
  zmk_keymap_LedMap map = zmk_keymap_LedMap_init_zero;

  if (!layer_id_valid(layer_id) ||
      kp_rgb_map_restore_defaults((uint8_t)layer_id) < 0 ||
      kp_rgb_map_read((uint8_t)layer_id, map.colors) < 0) {
    return ZMK_RPC_SIMPLE_ERR(GENERIC);
  }
  map.layer_id = layer_id;

  return KEYMAP_RESPONSE(restore_led_defaults, map);
}

/* Tells the client that the answer of get_led_info.unsaved_changes changed. */
void kp_rgb_studio_notify_unsaved(bool unsaved) {
  raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
      .notification = ZMK_RPC_NOTIFICATION(keymap, unsaved_led_changes_status_changed,
                                           unsaved)});
}

/* ZMK Studio's "restore stock settings" puts the maps back to the devicetree. */
static int keypaw_rgb_maps_reset(void) { return kp_rgb_maps_settings_reset(); }

ZMK_RPC_SUBSYSTEM_SETTINGS_RESET(keypaw_rgb_maps, keypaw_rgb_maps_reset);

ZMK_RPC_SUBSYSTEM_HANDLER(keymap, get_led_info, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, get_led_map, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, set_led_colors, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, save_led_changes, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, discard_led_changes, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, restore_led_defaults, ZMK_STUDIO_RPC_HANDLER_SECURED);

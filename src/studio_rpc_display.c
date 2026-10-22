/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK Studio access to the display settings (display_settings.c).
 *
 * The requests extend the keymap subsystem of the zmk-studio-messages fork
 * (lenerdz/zmk-studio-messages): their request ids are 106 to 108. Every handler
 * is secured, like the keymap ones: the keyboard has to be unlocked.
 *
 * A change is applied and kept at once: there is no save or discard step, and
 * so no notification either.
 *
 * Central only: ZMK Studio runs there. The peripheral is kept up to date by the
 * split sync (rgb_split_sync.c).
 */

#include <errno.h>
#include <stdint.h>

#include <pb_encode.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/studio/rpc.h>

#include "display_settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEYMAP_RESPONSE(type, ...) ZMK_RPC_RESPONSE(keymap, type, __VA_ARGS__)

/* DisplayInfo.settings has no max_count in keymap.options.in, so nanopb makes
 * it a callback: the list can be as long as the firmware's table, and the
 * response does not grow the union that ZMK Studio's thread keeps on its stack. */
static bool encode_display_settings(pb_ostream_t *stream, const pb_field_t *field,
                                    void *const *arg) {
  ARG_UNUSED(arg);

  for (size_t i = 0; i < kp_display_setting_count(); i++) {
    struct kp_display_setting_info setting;

    if (kp_display_setting_info(i, &setting) < 0) {
      break;
    }

    zmk_keymap_DisplaySetting msg = zmk_keymap_DisplaySetting_init_zero;

    msg.id = setting.id;
    msg.value = setting.value;
    msg.min = setting.min;
    msg.max = setting.max;
    msg.default_value = setting.default_value;

    if (!pb_encode_tag_for_field(stream, field) ||
        !pb_encode_submessage(stream, &zmk_keymap_DisplaySetting_msg, &msg)) {
      return false;
    }
  }
  return true;
}

static void fill_display_info(zmk_keymap_DisplayInfo *info) {
  info->settings.funcs.encode = encode_display_settings;
  info->settings.arg = NULL;
}

static zmk_studio_Response get_display_info(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_DisplayInfo info = zmk_keymap_DisplayInfo_init_zero;

  fill_display_info(&info);

  return KEYMAP_RESPONSE(get_display_info, info);
}

static zmk_studio_Response set_display_setting(const zmk_studio_Request *req) {
  const zmk_keymap_SetDisplaySettingRequest *set_req =
      &req->subsystem.keymap.request_type.set_display_setting;
  zmk_keymap_SetDisplaySettingResponse resp =
      zmk_keymap_SetDisplaySettingResponse_SET_DISPLAY_SETTING_RESP_OK;

  if (set_req->id > UINT8_MAX) {
    return KEYMAP_RESPONSE(
        set_display_setting,
        zmk_keymap_SetDisplaySettingResponse_SET_DISPLAY_SETTING_RESP_INVALID_ID);
  }

  int ret = kp_display_setting_set((uint8_t)set_req->id, set_req->value);

  switch (ret) {
  case 0:
    break;
  case -ENOENT:
    resp = zmk_keymap_SetDisplaySettingResponse_SET_DISPLAY_SETTING_RESP_INVALID_ID;
    break;
  case -EINVAL:
    resp = zmk_keymap_SetDisplaySettingResponse_SET_DISPLAY_SETTING_RESP_INVALID_VALUE;
    break;
  default:
    LOG_WRN("Setting a display setting failed with %d", ret);
    return ZMK_RPC_SIMPLE_ERR(GENERIC);
  }

  return KEYMAP_RESPONSE(set_display_setting, resp);
}

static zmk_studio_Response restore_display_defaults(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_DisplayInfo info = zmk_keymap_DisplayInfo_init_zero;

  if (kp_display_settings_restore_defaults() < 0) {
    return ZMK_RPC_SIMPLE_ERR(GENERIC);
  }
  fill_display_info(&info);

  return KEYMAP_RESPONSE(restore_display_defaults, info);
}

/* ZMK Studio's "restore stock settings" puts the display settings back to their
 * defaults. */
static int keypaw_display_reset(void) { return kp_display_settings_settings_reset(); }

ZMK_RPC_SUBSYSTEM_SETTINGS_RESET(keypaw_display, keypaw_display_reset);

ZMK_RPC_SUBSYSTEM_HANDLER(keymap, get_display_info, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, set_display_setting, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, restore_display_defaults, ZMK_STUDIO_RPC_HANDLER_SECURED);

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

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/studio/rpc.h>

#include "display_settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEYMAP_RESPONSE(type, ...) ZMK_RPC_RESPONSE(keymap, type, __VA_ARGS__)

/* The wire array is fixed (keymap.options.in); the firmware must fit it. */
BUILD_ASSERT(KP_DISPLAY_SETTING_COUNT <=
                 sizeof(((zmk_keymap_DisplayInfo *)0)->settings) /
                     sizeof(zmk_keymap_DisplaySetting),
             "more display settings than the DisplayInfo of the protocol can "
             "list: raise settings max_count in zmk-studio-messages");

static void fill_display_info(zmk_keymap_DisplayInfo *info) {
  size_t count = 0;

  for (size_t i = 0; i < kp_display_setting_count() &&
                     i < sizeof(info->settings) / sizeof(info->settings[0]);
       i++) {
    struct kp_display_setting_info setting;

    if (kp_display_setting_info(i, &setting) < 0) {
      break;
    }
    info->settings[count].id = setting.id;
    info->settings[count].value = setting.value;
    info->settings[count].min = setting.min;
    info->settings[count].max = setting.max;
    info->settings[count].default_value = setting.default_value;
    count++;
  }
  info->settings_count = (pb_size_t)count;
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

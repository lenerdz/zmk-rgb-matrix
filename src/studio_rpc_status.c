/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK Studio access to what the keyboard is doing and to what it is: the live
 * status (what the screens draw: the battery of both halves, the layer, the WPM,
 * the output and the Bluetooth profile) and the firmware information (the
 * keyboard, the board, the build, the flash and the RAM it uses).
 *
 * The requests extend the keymap subsystem of the zmk-studio-messages fork
 * (lenerdz/zmk-studio-messages): their request ids are 109 and 110. Every handler
 * is secured, like the keymap ones: the keyboard has to be unlocked.
 *
 * Read only: nothing here changes the keyboard, and nothing is kept, so there is
 * no save step and no notification. ZMK Studio asks again and again to keep a live
 * view, which is why every value is read straight from ZMK, with no copy.
 *
 * Central only: ZMK Studio runs there. Everything is the central's, except two
 * values about the peripheral that the central knows: whether it is connected,
 * and its battery level.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/studio/rpc.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

/* What the central knows about its peripheral comes from the split transport,
 * which only a central has. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#define KP_STATUS_HAS_PERIPHERAL 1
#else
#define KP_STATUS_HAS_PERIPHERAL 0
#endif

#include "kp_build_info.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEYMAP_RESPONSE(type, ...) ZMK_RPC_RESPONSE(keymap, type, __VA_ARGS__)

/* The value of a battery level that was not read: a half that has not reported
 * yet, or a build without battery reporting. */
#define KP_STATUS_NO_READING 255

/* The flash for the program and the RAM, from the devicetree: the same sizes the
 * linker uses for the memory report of the build. */
#if DT_HAS_CHOSEN(zephyr_code_partition)
#define KP_STATUS_FLASH_TOTAL DT_REG_SIZE(DT_CHOSEN(zephyr_code_partition))
#else
#define KP_STATUS_FLASH_TOTAL 0
#endif

#if DT_HAS_CHOSEN(zephyr_sram)
#define KP_STATUS_RAM_TOTAL DT_REG_SIZE(DT_CHOSEN(zephyr_sram))
#else
#define KP_STATUS_RAM_TOTAL 0
#endif

/* Copies a string into a field of a message, cut to fit and always ended. */
static void copy_string(char *dst, size_t size, const char *src) {
  if (size == 0) {
    return;
  }
  size_t len = (src == NULL) ? 0 : strnlen(src, size - 1);

  if (len > 0) {
    memcpy(dst, src, len);
  }
  dst[len] = '\0';
}

#if KP_STATUS_HAS_PERIPHERAL
/* The transport that serves the peripheral now: the first available one, in
 * registration order, which is priority order (the same choice as the split sync
 * of this module, rgb_split_sync.c). A NULL get_status means "always available". */
static const struct zmk_split_transport_central *pick_transport(void) {
  STRUCT_SECTION_FOREACH(zmk_split_transport_central, t) {
    if (t->api == NULL || t->api->get_available_source_ids == NULL) {
      continue;
    }
    if (t->api->get_status != NULL && !t->api->get_status().available) {
      continue;
    }
    return t;
  }
  return NULL;
}
#endif

static void fill_peripheral(zmk_keymap_LiveStatus *status) {
  status->battery_peripheral = KP_STATUS_NO_READING;
  status->peripheral_connected = false;

#if KP_STATUS_HAS_PERIPHERAL
  const struct zmk_split_transport_central *transport = pick_transport();

  if (transport == NULL) {
    return;
  }

  uint8_t ids[MAX(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT, 1)];
  int count = transport->api->get_available_source_ids(ids);

  if (count <= 0) {
    return;
  }
  status->peripheral_connected = true;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
  uint8_t level = 0;

  if (zmk_split_central_get_peripheral_battery_level(ids[0], &level) == 0) {
    status->battery_peripheral = MIN(level, 100);
  }
#endif
#endif
}

static void fill_layer(zmk_keymap_LiveStatus *status) {
  zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();

  status->layer_index = index;
  copy_string(status->layer_name, sizeof(status->layer_name),
              zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)));
}

static void fill_link(zmk_keymap_LiveStatus *status) {
  status->output = zmk_keymap_LiveOutput_LIVE_OUTPUT_NONE;
  status->ble_profile = 0;
  status->ble_state = zmk_keymap_LiveBleState_LIVE_BLE_OPEN;
  status->usb_state = zmk_keymap_LiveUsbState_LIVE_USB_NONE;

  struct zmk_endpoint_instance endpoint = zmk_endpoint_get_selected();

  if (endpoint.transport == ZMK_TRANSPORT_USB) {
    status->output = zmk_keymap_LiveOutput_LIVE_OUTPUT_USB;
  } else if (endpoint.transport == ZMK_TRANSPORT_BLE) {
    status->output = zmk_keymap_LiveOutput_LIVE_OUTPUT_BLE;
  }

#if IS_ENABLED(CONFIG_ZMK_USB)
  switch (zmk_usb_get_conn_state()) {
  case ZMK_USB_CONN_POWERED:
    status->usb_state = zmk_keymap_LiveUsbState_LIVE_USB_POWERED;
    break;
  case ZMK_USB_CONN_HID:
    status->usb_state = zmk_keymap_LiveUsbState_LIVE_USB_HID;
    break;
  default:
    break;
  }
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE)
  int profile = zmk_ble_active_profile_index();

  if (profile >= 0) {
    status->ble_profile = profile;
    if (zmk_ble_profile_is_connected(profile)) {
      status->ble_state = zmk_keymap_LiveBleState_LIVE_BLE_CONNECTED;
    } else if (zmk_ble_profile_is_open(profile)) {
      status->ble_state = zmk_keymap_LiveBleState_LIVE_BLE_OPEN;
    } else {
      status->ble_state = zmk_keymap_LiveBleState_LIVE_BLE_PAIRED;
    }
  }
#endif
}

static zmk_studio_Response get_live_status(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_LiveStatus status = zmk_keymap_LiveStatus_init_zero;

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
  uint8_t charge = zmk_battery_state_of_charge();

  status.battery_central = MIN(charge, 100);
#else
  status.battery_central = KP_STATUS_NO_READING;
#endif

  fill_peripheral(&status);
  fill_layer(&status);
  fill_link(&status);

#if IS_ENABLED(CONFIG_ZMK_WPM)
  int wpm = zmk_wpm_get_state();

  status.wpm = (wpm > 0) ? (uint32_t)wpm : 0;
#endif

  status.uptime_s = (uint32_t)(k_uptime_get() / MSEC_PER_SEC);

  return KEYMAP_RESPONSE(get_live_status, status);
}

static zmk_studio_Response get_firmware_info(const zmk_studio_Request *req) {
  ARG_UNUSED(req);
  zmk_keymap_FirmwareInfo info = zmk_keymap_FirmwareInfo_init_zero;

  copy_string(info.keyboard, sizeof(info.keyboard), CONFIG_ZMK_KEYBOARD_NAME);
  copy_string(info.board, sizeof(info.board), CONFIG_BOARD);
  copy_string(info.zephyr, sizeof(info.zephyr), KERNEL_VERSION_STRING);
  copy_string(info.build_id, sizeof(info.build_id), KP_BUILD_ID);
  copy_string(info.build_branch, sizeof(info.build_branch), KP_BUILD_BRANCH);
  info.build_number = KP_BUILD_NUMBER;
  copy_string(info.build_date, sizeof(info.build_date), KP_BUILD_DATE);

  /* _flash_used and _image_ram_size are absolute symbols of the linker script:
   * their value is their address. They are the numbers the linker prints as FLASH
   * and RAM at the end of the build. */
  info.flash_total = KP_STATUS_FLASH_TOTAL;
  info.flash_used = (uint32_t)(uintptr_t)_flash_used;
  info.ram_total = KP_STATUS_RAM_TOTAL;
  info.ram_used = (uint32_t)(uintptr_t)_image_ram_size;

  return KEYMAP_RESPONSE(get_firmware_info, info);
}

ZMK_RPC_SUBSYSTEM_HANDLER(keymap, get_live_status, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, get_firmware_info, ZMK_STUDIO_RPC_HANDLER_SECURED);

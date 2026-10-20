/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Display (OLED) settings (CONFIG_KEYPAW_DISPLAY_SETTINGS).
 *
 * A short table of numbered integers (include/zmk/display_settings.h). Each half
 * keeps all of them:
 *
 *   - ZMK Studio edits them on the central (studio_rpc_display.c). A change is
 *     applied at once, written to flash a few seconds after the last change
 *     (CONFIG_KEYPAW_DISPLAY_SAVE_DEBOUNCE_MS, so that dragging a slider writes
 *     once), and queued for the peripheral (rgb_split_sync.c, the same machinery
 *     that carries the colour maps).
 *   - The peripheral receives them as RGB_DISPLAY_SET_CMD (behavior_rgb_matrix.c),
 *     applies them and keeps them in its own flash, so its screen is right at
 *     boot, before the central connects.
 *
 * What the settings do:
 *
 *   - Contrast: display_set_contrast() of this half's screen, one setting per
 *     half. Applied shortly after boot and at every change.
 *   - Screen off: the module blanks the screen by itself, after the keyboard has
 *     been idle for the configured time and, in the "only on battery" mode, only
 *     while USB is not connected to this half. This replaces ZMK's own blanking,
 *     which cannot tell USB from battery (CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE has to
 *     be off). Any activity wakes the screen.
 *   - Toast: read by the zmk-nice-oled toast widget with kp_display_setting_get().
 *
 * Every call into the display driver runs on the display work queue, the one
 * LVGL flushes from, so two I2C sequences never interleave.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

#include "display_settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KP_DISPLAY_SUBTREE "keypaw/display"

/* The peripheral of a split keyboard drives the right screen; the central, or a
 * keyboard that is not split, drives the left one. */
#define KP_DISPLAY_PERIPHERAL                                                  \
  (IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

/* ZMK goes idle after this many seconds without activity. */
#define KP_DISPLAY_IDLE_S MAX(1, CONFIG_ZMK_IDLE_TIMEOUT / 1000)

/* The contrast the SSD1306 driver sets at boot. */
#ifdef CONFIG_SSD1306_DEFAULT_CONTRAST
#define KP_DISPLAY_CONTRAST_DEFAULT CONFIG_SSD1306_DEFAULT_CONTRAST
#else
#define KP_DISPLAY_CONTRAST_DEFAULT 128
#endif

/* After a change made in ZMK Studio the screen wakes and stays on at least this
 * long, so the change can be seen. */
#define KP_DISPLAY_WAKE_MIN_S 10

struct kp_display_def {
  uint8_t id;
  int32_t min;
  int32_t max;
  int32_t def;
};

static const struct kp_display_def kp_display_defs[] = {
    {KP_DISPLAY_LEFT_CONTRAST, 0, 255, KP_DISPLAY_CONTRAST_DEFAULT},
    {KP_DISPLAY_RIGHT_CONTRAST, 0, 255, KP_DISPLAY_CONTRAST_DEFAULT},
    {KP_DISPLAY_OFF_MODE, KP_DISPLAY_OFF_NEVER, KP_DISPLAY_OFF_ALWAYS,
     KP_DISPLAY_OFF_NEVER},
    {KP_DISPLAY_OFF_AFTER_S, KP_DISPLAY_IDLE_S, 3600,
     MAX(60, KP_DISPLAY_IDLE_S)},
    {KP_DISPLAY_TOAST, 0, 1, 1},
    {KP_DISPLAY_TOAST_MS, 500, 5000, CONFIG_KEYPAW_DISPLAY_TOAST_MS_DEFAULT},
};

/* The ids travel in 4 bits (RGB_DISPLAY_PARAM1). */
BUILD_ASSERT(KP_DISPLAY_TOAST_MS < 16, "display setting ids have to be below 16");
BUILD_ASSERT(ARRAY_SIZE(kp_display_defs) == KP_DISPLAY_SETTING_COUNT,
             "KP_DISPLAY_SETTING_COUNT (zmk/display_settings.h) does not match "
             "the table");
/* One dirty bit per entry. */
BUILD_ASSERT(ARRAY_SIZE(kp_display_defs) <= 32,
             "kp_display_dirty has 32 bits");

static int32_t kp_display_values[ARRAY_SIZE(kp_display_defs)];
static K_MUTEX_DEFINE(kp_display_lock);

static int kp_display_index(uint8_t id) {
  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    if (kp_display_defs[i].id == id) {
      return (int)i;
    }
  }
  return -ENOENT;
}

static bool kp_display_in_range(size_t index, int32_t value) {
  return value >= kp_display_defs[index].min &&
         value <= kp_display_defs[index].max;
}

int32_t kp_display_setting_get(enum kp_display_setting_id id) {
  int index = kp_display_index((uint8_t)id);
  return index < 0 ? 0 : kp_display_values[index];
}

size_t kp_display_setting_count(void) { return ARRAY_SIZE(kp_display_defs); }

int kp_display_setting_info(size_t index, struct kp_display_setting_info *out) {
  if (index >= ARRAY_SIZE(kp_display_defs)) {
    return -EINVAL;
  }
  out->id = kp_display_defs[index].id;
  out->value = kp_display_values[index];
  out->min = kp_display_defs[index].min;
  out->max = kp_display_defs[index].max;
  out->default_value = kp_display_defs[index].def;
  return 0;
}

bool kp_display_sync_get(size_t index, uint32_t *param1, uint32_t *param2) {
  if (index >= ARRAY_SIZE(kp_display_defs)) {
    return false;
  }
  *param1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, kp_display_defs[index].id);
  *param2 = (uint32_t)kp_display_values[index];
  return true;
}

/* ---- flash ------------------------------------------------------------------ */

/* Bit i is set while the record of entry i has to be written. */
static atomic_t kp_display_dirty;

/* A setting at its default has no record. */
static void kp_display_save_handler(struct k_work *work) {
  ARG_UNUSED(work);

  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    if (!atomic_test_and_clear_bit(&kp_display_dirty, i)) {
      continue;
    }

    char path[sizeof(KP_DISPLAY_SUBTREE) + 4];
    snprintk(path, sizeof(path), KP_DISPLAY_SUBTREE "/%u",
             (uint32_t)kp_display_defs[i].id);

    int32_t value = kp_display_values[i];
    int err = value == kp_display_defs[i].def
                  ? settings_delete(path)
                  : settings_save_one(path, &value, sizeof(value));
    if (err < 0 && err != -ENOENT) {
      LOG_WRN("Failed to keep display setting %u (err %d)",
              (uint32_t)kp_display_defs[i].id, err);
    }
  }
}

static K_WORK_DELAYABLE_DEFINE(kp_display_save_work, kp_display_save_handler);

static void kp_display_request_save(size_t index) {
  atomic_set_bit(&kp_display_dirty, index);
  k_work_reschedule(&kp_display_save_work,
                    K_MSEC(CONFIG_KEYPAW_DISPLAY_SAVE_DEBOUNCE_MS));
}

/* Records that cannot be used are ignored, never an error: a failing handler
 * could stop the load of the settings that come after it. */
static int kp_display_load_cb(const char *name, size_t len,
                              settings_read_cb read_cb, void *cb_arg) {
  char *end;
  long id = strtol(name, &end, 10);

  if (end == name || *end != '\0' || id < 0 || id > UINT8_MAX) {
    return 0;
  }

  int index = kp_display_index((uint8_t)id);
  int32_t value;

  if (index < 0 || len != sizeof(value)) {
    return 0;
  }

  int rc = read_cb(cb_arg, &value, sizeof(value));

  if (rc < 0) {
    return rc;
  }
  if (rc == sizeof(value) && kp_display_in_range((size_t)index, value)) {
    kp_display_values[index] = value;
  }
  return 0;
}

/* ---- the screen -------------------------------------------------------------- */

#if DT_HAS_CHOSEN(zephyr_display)

static const struct device *const kp_display_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/* Touched only by the display work queue. */
static bool kp_display_blanked;

static void kp_display_submit(struct k_work *work) {
  int ret = k_work_submit_to_queue(zmk_display_work_q(), work);

  if (ret < 0) {
    LOG_WRN("The display work queue is not ready (err %d)", ret);
  }
}

static void kp_display_contrast_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (!device_is_ready(kp_display_dev)) {
    return;
  }

  int32_t contrast = kp_display_setting_get(
      KP_DISPLAY_PERIPHERAL ? KP_DISPLAY_RIGHT_CONTRAST
                            : KP_DISPLAY_LEFT_CONTRAST);
  int err = display_set_contrast(kp_display_dev, (uint8_t)contrast);

  if (err < 0) {
    LOG_WRN("Setting the display contrast failed (err %d)", err);
  }
}

static K_WORK_DEFINE(kp_display_contrast_work, kp_display_contrast_handler);

static bool kp_display_usb_powered(void) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
  return zmk_usb_is_powered();
#else
  return false;
#endif
}

/* True while the screen should be off: the keyboard is idle and the mode allows
 * it. Any context. */
static bool kp_display_blank_wanted(void) {
  if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE) {
    return false;
  }

  switch (kp_display_setting_get(KP_DISPLAY_OFF_MODE)) {
  case KP_DISPLAY_OFF_ALWAYS:
    return true;
  case KP_DISPLAY_OFF_ON_BATTERY:
    return !kp_display_usb_powered();
  default:
    return false;
  }
}

static void kp_display_blank_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (kp_display_blanked || !kp_display_blank_wanted() ||
      !device_is_ready(kp_display_dev)) {
    return;
  }

  int err = display_blanking_on(kp_display_dev);

  if (err < 0) {
    LOG_WRN("Turning the display off failed (err %d)", err);
    return;
  }
  kp_display_blanked = true;
}

static K_WORK_DELAYABLE_DEFINE(kp_display_blank_work, kp_display_blank_handler);

static void kp_display_unblank_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (!kp_display_blanked) {
    return;
  }

  int err = display_blanking_off(kp_display_dev);

  if (err < 0) {
    LOG_WRN("Turning the display on failed (err %d)", err);
    return;
  }
  kp_display_blanked = false;
}

static K_WORK_DEFINE(kp_display_unblank_work, kp_display_unblank_handler);

/* Brings the screen in line with the activity state, the USB state and the
 * settings: turns it back on when it should not be off, and arms the timer that
 * turns it off when it should. `wake` is for a change the user just made: the
 * screen turns on and stays on for at least KP_DISPLAY_WAKE_MIN_S. */
static void kp_display_policy_update(bool wake) {
  if (!kp_display_blank_wanted()) {
    k_work_cancel_delayable(&kp_display_blank_work);
    kp_display_submit(&kp_display_unblank_work);
    return;
  }

  int32_t after_idle_s =
      kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S) - KP_DISPLAY_IDLE_S;
  uint32_t delay_s = (uint32_t)MAX(after_idle_s, 0);

  if (wake) {
    delay_s = MAX(delay_s, (uint32_t)KP_DISPLAY_WAKE_MIN_S);
    kp_display_submit(&kp_display_unblank_work);
    k_work_reschedule_for_queue(zmk_display_work_q(), &kp_display_blank_work,
                                K_SECONDS(delay_s));
  } else {
    /* An armed timer keeps its time. */
    k_work_schedule_for_queue(zmk_display_work_q(), &kp_display_blank_work,
                              K_SECONDS(delay_s));
  }
}

static int kp_display_event_handler(const zmk_event_t *eh) {
  ARG_UNUSED(eh);
  kp_display_policy_update(false);
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kp_display_settings, kp_display_event_handler);
ZMK_SUBSCRIPTION(kp_display_settings, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(kp_display_settings, zmk_usb_conn_state_changed);
#endif

/* The saved contrast is applied once the display has been set up. */
static void kp_display_boot_handler(struct k_work *work) {
  ARG_UNUSED(work);
  kp_display_submit(&kp_display_contrast_work);
}

static K_WORK_DELAYABLE_DEFINE(kp_display_boot_work, kp_display_boot_handler);

static void kp_display_schedule_boot_apply(uint32_t delay_ms) {
  k_work_reschedule(&kp_display_boot_work, K_MSEC(delay_ms));
}

static void kp_display_apply_changed(uint8_t id) {
  switch (id) {
  case KP_DISPLAY_LEFT_CONTRAST:
  case KP_DISPLAY_RIGHT_CONTRAST:
    kp_display_submit(&kp_display_contrast_work);
    kp_display_policy_update(true);
    break;
  case KP_DISPLAY_OFF_MODE:
  case KP_DISPLAY_OFF_AFTER_S:
    kp_display_policy_update(true);
    break;
  default:
    break;
  }
}

#else /* !DT_HAS_CHOSEN(zephyr_display) */

static void kp_display_schedule_boot_apply(uint32_t delay_ms) {
  ARG_UNUSED(delay_ms);
}

static void kp_display_apply_changed(uint8_t id) { ARG_UNUSED(id); }

#endif /* DT_HAS_CHOSEN(zephyr_display) */

/* The settings are loaded (or there is nothing saved) when this runs. */
static int kp_display_commit_cb(void) {
  kp_display_schedule_boot_apply(500);
  return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(kp_display, KP_DISPLAY_SUBTREE, NULL,
                               kp_display_load_cb, kp_display_commit_cb, NULL);

static int kp_display_settings_init(void) {
  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    kp_display_values[i] = kp_display_defs[i].def;
  }

  /* In case no settings load follows. */
  kp_display_schedule_boot_apply(3000);
  return 0;
}

SYS_INIT(kp_display_settings_init, POST_KERNEL,
         CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/* ---- changes -------------------------------------------------------------- */

static void kp_display_changed(size_t index) {
  kp_display_request_save(index);
  kp_display_apply_changed(kp_display_defs[index].id);
  /* Does nothing on a peripheral. */
  kp_rgb_sync_display_kick();
}

int kp_display_setting_set(uint8_t id, int32_t value) {
  int index = kp_display_index(id);

  if (index < 0) {
    return -ENOENT;
  }
  if (!kp_display_in_range((size_t)index, value)) {
    return -EINVAL;
  }

  k_mutex_lock(&kp_display_lock, K_FOREVER);
  bool changed = kp_display_values[index] != value;
  kp_display_values[index] = value;
  k_mutex_unlock(&kp_display_lock);

  if (changed) {
    kp_display_changed((size_t)index);
  }
  return 0;
}

int kp_display_settings_restore_defaults(void) {
  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    (void)kp_display_setting_set(kp_display_defs[i].id, kp_display_defs[i].def);
  }
  return 0;
}

int kp_display_settings_settings_reset(void) {
  struct k_work_sync sync;

  (void)kp_display_settings_restore_defaults();

  /* The records go now, not after the debounce: the reset is a one-off. */
  k_work_cancel_delayable_sync(&kp_display_save_work, &sync);
  atomic_set(&kp_display_dirty, BIT_MASK(ARRAY_SIZE(kp_display_defs)));
  kp_display_save_handler(NULL);
  return 0;
}

int kp_display_settings_command(uint32_t param1, uint32_t param2) {
  return kp_display_setting_set(RGB_DISPLAY_PARAM1_ID(param1), (int32_t)param2);
}

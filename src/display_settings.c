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
 *   - OLED layout (CONFIG_KEYPAW_OLED_LAYOUT): which widgets of the zmk-nice-oled
 *     screens are drawn, where, and which animation the right screen plays. The
 *     widgets read the values with kp_display_setting_get(), and the event
 *     zmk_display_settings_changed tells them to draw again.
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
#if IS_ENABLED(CONFIG_KEYPAW_OLED_LAYOUT)
#include <zmk/events/display_settings_changed.h>
#endif
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
  /* The peripheral applies it, so the central pushes it there. */
  bool to_peripheral;
};

#if IS_ENABLED(CONFIG_KEYPAW_OLED_LAYOUT)

/* The screen of this build is the left one (the central, or a keyboard that is
 * not split). The right one is always listed: the central keeps and pushes it. */
#define KP_OLED_LEFT_HERE                                                      \
  (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#define KP_OLED_SHOW(id, peripheral) {id, 0, 1, 1, peripheral}
#define KP_OLED_COORD(id, def, peripheral)                                     \
  {id, KP_OLED_COORD_MIN, KP_OLED_COORD_MAX, def, peripheral}

/* What the right screen draws by default: the choice of the build, as
 * zmk-nice-oled makes it (CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_*). */
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL)
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_HEAD)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_HEAD
#elif IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CAT)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_CAT
#elif IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SPACEMAN)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_SPACEMAN
#elif IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_POKEMON)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_POKEMON
#else
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_CRYSTAL
#endif
#elif IS_ENABLED(CONFIG_NICE_OLED_WIDGET_STATIC_IMAGE_PERIPHERAL_VIM)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_VIM
#elif IS_ENABLED(CONFIG_NICE_OLED_WIDGET_STATIC_IMAGE_PERIPHERAL_VIP_MARCOS)
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_VIP_MARCOS
#else
#define KP_OLED_ANIM_KIND_DEFAULT KP_OLED_ANIM_CRYSTAL
#endif

/* The battery of this half is the text of both halves where the build asks for
 * it (zmk-nice-oled draws that at a fixed place). */
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_CENTRAL_SHOW_BATTERY_PERIPHERAL_ALL) ||  \
    IS_ENABLED(CONFIG_NICE_OLED_WIDGET_CENTRAL_SHOW_BATTERY_PERIPHERAL_ONLY) || \
    IS_ENABLED(CONFIG_NICE_OLED_WIDGET_CENTRAL_SHOW_BATTERY_PERIPHERAL_AND_CENTRAL)
#define KP_OLED_L_BATTERY_X_DEFAULT 0
#define KP_OLED_L_BATTERY_Y_DEFAULT 19
#else
#define KP_OLED_L_BATTERY_X_DEFAULT CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X
#define KP_OLED_L_BATTERY_Y_DEFAULT CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y
#endif

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_PROFILE_BIG)
#define KP_OLED_L_PROFILE_X_DEFAULT 0
#define KP_OLED_L_PROFILE_Y_DEFAULT 129
#else
#define KP_OLED_L_PROFILE_X_DEFAULT CONFIG_NICE_OLED_WIDGET_PROFILE_CUSTOM_X
#define KP_OLED_L_PROFILE_Y_DEFAULT CONFIG_NICE_OLED_WIDGET_PROFILE_CUSTOM_Y
#endif

/* The grid under the WPM graph has no option: zmk-nice-oled draws it here. */
#define KP_OLED_L_WPM_GRAPH_X_DEFAULT (-1)
#define KP_OLED_L_WPM_GRAPH_Y_DEFAULT 95

#endif /* CONFIG_KEYPAW_OLED_LAYOUT */

static const struct kp_display_def kp_display_defs[] = {
    {KP_DISPLAY_LEFT_CONTRAST, 0, 255, KP_DISPLAY_CONTRAST_DEFAULT, false},
    {KP_DISPLAY_RIGHT_CONTRAST, 0, 255, KP_DISPLAY_CONTRAST_DEFAULT, true},
    {KP_DISPLAY_OFF_MODE, KP_DISPLAY_OFF_NEVER, KP_DISPLAY_OFF_ALWAYS,
     KP_DISPLAY_OFF_NEVER, true},
    {KP_DISPLAY_OFF_AFTER_S, KP_DISPLAY_IDLE_S, 3600,
     MAX(60, KP_DISPLAY_IDLE_S), true},
    {KP_DISPLAY_TOAST, 0, 1, 1, true},
    {KP_DISPLAY_TOAST_MS, 500, 5000, CONFIG_KEYPAW_DISPLAY_TOAST_MS_DEFAULT,
     true},
#if IS_ENABLED(CONFIG_KEYPAW_OLED_LAYOUT)
    /* Right screen. */
#if !IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
    KP_OLED_SHOW(KP_OLED_R_ANIM_SHOW, true),
    KP_OLED_COORD(KP_OLED_R_ANIM_X,
                  CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CUSTOM_X, true),
    KP_OLED_COORD(KP_OLED_R_ANIM_Y,
                  CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CUSTOM_Y, true),
    {KP_OLED_R_ANIM_KIND, 0, KP_OLED_ANIM_KIND_COUNT - 1,
     KP_OLED_ANIM_KIND_DEFAULT, true},
    {KP_OLED_R_ANIM_MS, 0, 60000, 0, true},
#endif
    KP_OLED_SHOW(KP_OLED_R_BATTERY_SHOW, true),
    KP_OLED_COORD(KP_OLED_R_BATTERY_X, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X,
                  true),
    KP_OLED_COORD(KP_OLED_R_BATTERY_Y, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y,
                  true),
    KP_OLED_SHOW(KP_OLED_R_LINK_SHOW, true),
    KP_OLED_COORD(KP_OLED_R_LINK_X, CONFIG_NICE_OLED_WIDGET_OUTPUT_BT_CUSTOM_X,
                  true),
    KP_OLED_COORD(KP_OLED_R_LINK_Y, CONFIG_NICE_OLED_WIDGET_OUTPUT_BT_CUSTOM_Y,
                  true),
    KP_OLED_COORD(KP_OLED_R_TOAST_X, CONFIG_NICE_OLED_WIDGET_LED_TOAST_CUSTOM_X,
                  true),
    KP_OLED_COORD(KP_OLED_R_TOAST_Y, CONFIG_NICE_OLED_WIDGET_LED_TOAST_CUSTOM_Y,
                  true),
#if KP_OLED_LEFT_HERE
    /* Left screen. */
    KP_OLED_SHOW(KP_OLED_L_OUTPUT_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_OUTPUT_X, CONFIG_NICE_OLED_WIDGET_OUTPUT_USB_CUSTOM_X,
                  false),
    KP_OLED_COORD(KP_OLED_L_OUTPUT_Y, CONFIG_NICE_OLED_WIDGET_OUTPUT_USB_CUSTOM_Y,
                  false),
    KP_OLED_SHOW(KP_OLED_L_BATTERY_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_BATTERY_X, KP_OLED_L_BATTERY_X_DEFAULT, false),
    KP_OLED_COORD(KP_OLED_L_BATTERY_Y, KP_OLED_L_BATTERY_Y_DEFAULT, false),
    KP_OLED_SHOW(KP_OLED_L_PROFILE_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_PROFILE_X, KP_OLED_L_PROFILE_X_DEFAULT, false),
    KP_OLED_COORD(KP_OLED_L_PROFILE_Y, KP_OLED_L_PROFILE_Y_DEFAULT, false),
    KP_OLED_SHOW(KP_OLED_L_PROFILE_NUM_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_PROFILE_NUM_X,
                  CONFIG_NICE_OLED_WIDGET_PROFILE_TEXT_CUSTOM_X, false),
    KP_OLED_COORD(KP_OLED_L_PROFILE_NUM_Y,
                  CONFIG_NICE_OLED_WIDGET_PROFILE_TEXT_CUSTOM_Y, false),
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_LAYER)
    KP_OLED_SHOW(KP_OLED_L_LAYER_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_LAYER_X, CONFIG_NICE_OLED_WIDGET_LAYER_CUSTOM_X,
                  false),
    KP_OLED_COORD(KP_OLED_L_LAYER_Y, CONFIG_NICE_OLED_WIDGET_LAYER_CUSTOM_Y,
                  false),
#endif
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_WPM_SPEEDOMETER)
    KP_OLED_SHOW(KP_OLED_L_WPM_GAUGE_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_WPM_GAUGE_X,
                  CONFIG_NICE_OLED_WIDGET_WPM_GAUGE_CUSTOM_X, false),
    KP_OLED_COORD(KP_OLED_L_WPM_GAUGE_Y,
                  CONFIG_NICE_OLED_WIDGET_WPM_GAUGE_CUSTOM_Y, false),
#endif
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_WPM_NUMBER)
    KP_OLED_SHOW(KP_OLED_L_WPM_NUMBER_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_WPM_NUMBER_X,
                  CONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_X, false),
    KP_OLED_COORD(KP_OLED_L_WPM_NUMBER_Y,
                  CONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_Y, false),
#endif
/* zmk-nice-oled leaves the graph out where it makes room for the bongo cat,
 * the luna dog or the fixed modifier indicators. */
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_WPM_GRAPH) &&                           \
    !IS_ENABLED(CONFIG_NICE_OLED_WIDGET_WPM_LUNA) &&                           \
    !IS_ENABLED(CONFIG_NICE_OLED_WIDGET_WPM_BONGO_CAT) &&                      \
    !IS_ENABLED(CONFIG_NICE_OLED_WIDGET_MODIFIERS_INDICATORS_FIXED)
    KP_OLED_SHOW(KP_OLED_L_WPM_GRAPH_SHOW, false),
    KP_OLED_COORD(KP_OLED_L_WPM_GRAPH_X, KP_OLED_L_WPM_GRAPH_X_DEFAULT, false),
    KP_OLED_COORD(KP_OLED_L_WPM_GRAPH_Y, KP_OLED_L_WPM_GRAPH_Y_DEFAULT, false),
#endif
#endif /* KP_OLED_LEFT_HERE */
#endif /* CONFIG_KEYPAW_OLED_LAYOUT */
};

/* The ids travel in 8 bits (RGB_DISPLAY_PARAM1). */
BUILD_ASSERT(KP_OLED_L_WPM_GRAPH_Y < 256,
             "display setting ids have to be below 256");
/* The bitmap of changes and the copy of a peripheral's values have
 * KP_DISPLAY_SETTING_CAP entries. */
BUILD_ASSERT(ARRAY_SIZE(kp_display_defs) <= KP_DISPLAY_SETTING_CAP,
             "more display settings than KP_DISPLAY_SETTING_CAP "
             "(zmk/display_settings.h)");

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

int32_t kp_display_setting_default(enum kp_display_setting_id id) {
  int index = kp_display_index((uint8_t)id);
  return index < 0 ? 0 : kp_display_defs[index].def;
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

int kp_display_sync_next(size_t from, const int32_t *shadow) {
  for (size_t i = from; i < ARRAY_SIZE(kp_display_defs); i++) {
    if (kp_display_defs[i].to_peripheral &&
        shadow[i] != kp_display_values[i]) {
      return (int)i;
    }
  }
  return -1;
}

bool kp_display_sync_command(size_t index, uint32_t *param1, uint32_t *param2) {
  if (index >= ARRAY_SIZE(kp_display_defs)) {
    return false;
  }
  *param1 = RGB_DISPLAY_PARAM1(RGB_DISPLAY_SET_CMD, kp_display_defs[index].id);
  *param2 = (uint32_t)kp_display_values[index];
  return true;
}

/* ---- flash ------------------------------------------------------------------ */

/* Bit i is set while the record of entry i has to be written. */
static ATOMIC_DEFINE(kp_display_dirty, KP_DISPLAY_SETTING_CAP);

/* A setting at its default has no record. */
static void kp_display_save_handler(struct k_work *work) {
  ARG_UNUSED(work);

  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    if (!atomic_test_and_clear_bit(kp_display_dirty, i)) {
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
  atomic_set_bit(kp_display_dirty, index);
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

static void kp_display_notify_layout(uint8_t id);

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
  /* The widgets drew with the defaults if they came up before the load. */
  kp_display_notify_layout(0);
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

#if IS_ENABLED(CONFIG_KEYPAW_OLED_LAYOUT)

/* Changes that follow one another (restoring the defaults, the central pushing
 * the whole layout to a peripheral) make one event: the widgets draw the screen
 * again once, not once per setting. */
#define KP_DISPLAY_LAYOUT_EVENT_DELAY_MS 20

static volatile uint8_t kp_display_layout_last_id;

static void kp_display_layout_handler(struct k_work *work) {
  ARG_UNUSED(work);

  int err = raise_zmk_display_settings_changed(
      (struct zmk_display_settings_changed){.id = kp_display_layout_last_id});

  if (err < 0) {
    LOG_WRN("Raising the display settings event failed (err %d)", err);
  }
}

static K_WORK_DELAYABLE_DEFINE(kp_display_layout_work,
                               kp_display_layout_handler);

#endif /* CONFIG_KEYPAW_OLED_LAYOUT */

/* Tells the screen widgets that the OLED layout changed. `id` is the setting
 * that did, or 0 when any of them may have. Settings that are not part of the
 * layout (the contrast, the screen off, the toast switch) are applied by this
 * file and do not need the widgets. */
static void kp_display_notify_layout(uint8_t id) {
#if IS_ENABLED(CONFIG_KEYPAW_OLED_LAYOUT)
  if (id == 0 || id >= KP_OLED_R_ANIM_SHOW) {
    kp_display_layout_last_id = id;
    k_work_reschedule(&kp_display_layout_work,
                      K_MSEC(KP_DISPLAY_LAYOUT_EVENT_DELAY_MS));
  }
#else
  ARG_UNUSED(id);
#endif
}

static void kp_display_changed(size_t index) {
  kp_display_request_save(index);
  kp_display_apply_changed(kp_display_defs[index].id);
  kp_display_notify_layout(kp_display_defs[index].id);
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
  for (size_t i = 0; i < ARRAY_SIZE(kp_display_defs); i++) {
    atomic_set_bit(kp_display_dirty, i);
  }
  kp_display_save_handler(NULL);
  return 0;
}

int kp_display_settings_command(uint32_t param1, uint32_t param2) {
  return kp_display_setting_set(RGB_DISPLAY_PARAM1_ID(param1), (int32_t)param2);
}

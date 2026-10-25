/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * When the screen and the lights go off, and when the screen dims
 * (CONFIG_KEYPAW_DISPLAY_SETTINGS). The rules are in idle_policy.h; this file
 * keeps the time of the last activity, asks the rules what is off now, and tells
 * display_settings.c (the screen) and rgb_matrix.c (the LEDs) when that changed.
 *
 * Why not ZMK's own idle state: it is one timeout for everything (30 s), each half
 * counts only the keys it sees (the right half does not notice that you are typing
 * on the left one), and it knows nothing about USB. Here:
 *
 *   - the central counts the keys of both halves, which it receives from the
 *     peripheral, and tells the peripheral about its key presses and about its USB
 *     state (RGB_ACTIVITY_CMD, rgb_split_sync.c): at most one report every few
 *     seconds while typing, and one each time the USB state changes or the
 *     periodic clock of the split sync ticks;
 *   - each half has its own limits, kept in the display settings, and the
 *     peripheral takes the keyboard to have USB when the central does.
 *
 * Everything runs on the work queue of the RGB engine: the evaluation, and what it
 * triggers on the LEDs. The screen is driven from the display work queue
 * (display_settings.c).
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/display_settings.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

#include "display_settings.h"
#include "idle_policy.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* The work queue of the RGB engine (rgb_matrix_internal.h, whose header needs the
 * devicetree of the strip; this file does not, which lets a test on the PC build
 * it). */
struct k_work_q *kp_rgb_work_q(void);

/* What the central says about USB is trusted for this long after the last time it
 * said it. The central repeats it every CONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS (30 s
 * by default); a central that went away (the computer is off, the halves were
 * separated) stops being believed, and the peripheral counts only its own USB. */
#define KP_IDLE_PEER_TTL_MS 120000

#if IS_ENABLED(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
BUILD_ASSERT(2 * CONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS < KP_IDLE_PEER_TTL_MS,
             "the split sync clock is too slow for the peripheral to keep "
             "believing what the central says about USB (idle_policy.c)");
#endif

static struct k_spinlock kp_idle_lock;
/* The time of the last activity and what the central last said; under the lock. */
static int64_t kp_idle_last_activity_ms;
static int64_t kp_idle_peer_ping_ms;
static bool kp_idle_peer_valid;
static bool kp_idle_peer_usb;

/* Written by the evaluation only; read anywhere (a bool is read in one go). */
static volatile struct kp_idle_state kp_idle_state;
/* Something is off or dimmed: the next activity has to wake it at once. */
static volatile bool kp_idle_asleep;

static void kp_idle_eval_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(kp_idle_eval_work, kp_idle_eval_handler);

bool kp_idle_screen_off(void) { return kp_idle_state.screen_off; }
bool kp_idle_keys_off(void) { return kp_idle_state.keys_off; }
bool kp_idle_glow_off(void) { return kp_idle_state.glow_off; }
bool kp_idle_dimmed(void) { return kp_idle_state.dimmed; }

bool kp_idle_usb_local(void) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
  return zmk_usb_is_powered();
#else
  return false;
#endif
}

static void kp_idle_read_config(struct kp_idle_config *cfg) {
  cfg->screen_mode = (uint8_t)kp_display_setting_get(KP_DISPLAY_OFF_MODE);
  cfg->screen_after_s = (uint32_t)kp_display_setting_get(KP_DISPLAY_OFF_AFTER_S);
  cfg->keys_mode = (uint8_t)kp_display_setting_get(KP_DISPLAY_KEYS_OFF_MODE);
  cfg->keys_after_s =
      (uint32_t)kp_display_setting_get(KP_DISPLAY_KEYS_OFF_AFTER_S);
  cfg->glow_mode = (uint8_t)kp_display_setting_get(KP_DISPLAY_GLOW_OFF_MODE);
  cfg->glow_after_s =
      (uint32_t)kp_display_setting_get(KP_DISPLAY_GLOW_OFF_AFTER_S);
  cfg->dim_after_s = (uint32_t)kp_display_setting_get(KP_DISPLAY_DIM_AFTER_S);
}

void kp_idle_config_changed(void) {
  k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_idle_eval_work, K_NO_WAIT);
}

void kp_idle_note_activity(void) {
  k_spinlock_key_t key = k_spin_lock(&kp_idle_lock);
  kp_idle_last_activity_ms = k_uptime_get();
  k_spin_unlock(&kp_idle_lock, key);

  /* Nothing is off: the evaluation that is already armed will find the time
   * moved and arm itself again. Something is: wake it now. */
  if (kp_idle_asleep) {
    kp_idle_config_changed();
  }
}

void kp_idle_peer_update(bool usb, bool key) {
  k_spinlock_key_t lock = k_spin_lock(&kp_idle_lock);
  /* The cable of the central going in or out is somebody being there, as it is
   * for this half (kp_idle_event_handler); the first report after the start says
   * nothing about a change. */
  const bool usb_changed = kp_idle_peer_valid && kp_idle_peer_usb != usb;
  kp_idle_peer_usb = usb;
  kp_idle_peer_valid = true;
  kp_idle_peer_ping_ms = k_uptime_get();
  k_spin_unlock(&kp_idle_lock, lock);

  if (key || usb_changed) {
    kp_idle_note_activity();
  }
  /* The USB state of the keyboard may have changed. */
  kp_idle_config_changed();
}

static void kp_idle_eval_handler(struct k_work *work) {
  ARG_UNUSED(work);

  struct kp_idle_config cfg;
  kp_idle_read_config(&cfg);

  const int64_t now = k_uptime_get();
  const bool local_usb = kp_idle_usb_local();

  k_spinlock_key_t key = k_spin_lock(&kp_idle_lock);
  const int64_t seen = kp_idle_last_activity_ms;
  const int64_t peer_age = now - kp_idle_peer_ping_ms;
  const bool peer_usb =
      kp_idle_peer_valid && kp_idle_peer_usb && peer_age < KP_IDLE_PEER_TTL_MS;
  k_spin_unlock(&kp_idle_lock, key);

  struct kp_idle_state next;
  uint32_t wait = kp_idle_evaluate(&cfg, local_usb || peer_usb, now - seen, &next);

  if (peer_usb) {
    /* The USB of the central stops counting when its time is up. */
    const uint32_t left = (uint32_t)(KP_IDLE_PEER_TTL_MS - peer_age);

    if (left < wait) {
      wait = left;
    }
  }

  const struct kp_idle_state prev = {
      .screen_off = kp_idle_state.screen_off,
      .keys_off = kp_idle_state.keys_off,
      .glow_off = kp_idle_state.glow_off,
      .dimmed = kp_idle_state.dimmed,
  };

  kp_idle_state.screen_off = next.screen_off;
  kp_idle_state.keys_off = next.keys_off;
  kp_idle_state.glow_off = next.glow_off;
  kp_idle_state.dimmed = next.dimmed;
  kp_idle_asleep =
      next.screen_off || next.keys_off || next.glow_off || next.dimmed;

  if (next.screen_off != prev.screen_off || next.dimmed != prev.dimmed) {
    kp_display_idle_changed();
  }
  if (next.keys_off != prev.keys_off || next.glow_off != prev.glow_off) {
    kp_rgb_idle_changed();
  }

  /* A key pressed while this ran: look again right now. */
  key = k_spin_lock(&kp_idle_lock);
  const bool moved = kp_idle_last_activity_ms != seen;
  k_spin_unlock(&kp_idle_lock, key);

  if (moved) {
    wait = 0;
  }
  if (wait != KP_IDLE_NO_DEADLINE) {
    k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_idle_eval_work,
                                K_MSEC(wait));
  }
}

/* A key on either half is activity. The central also tells its peripheral about
 * the keys it presses itself; a key of the peripheral needs no report back. */
static int kp_idle_event_handler(const zmk_event_t *eh) {
  const struct zmk_position_state_changed *position =
      as_zmk_position_state_changed(eh);

  if (position != NULL) {
    kp_idle_note_activity();
    if (position->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
      kp_rgb_sync_activity_kick(true);
    }
    return ZMK_EV_EVENT_BUBBLE;
  }

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
  if (as_zmk_usb_conn_state_changed(eh) != NULL) {
    /* Plugging a cable in or out is somebody being there. */
    kp_idle_note_activity();
    kp_idle_config_changed();
    /* The peripheral has to know. */
    kp_rgb_sync_activity_kick(false);
  }
#endif
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kp_idle_policy, kp_idle_event_handler);
ZMK_SUBSCRIPTION(kp_idle_policy, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(kp_idle_policy, zmk_usb_conn_state_changed);
#endif

static int kp_idle_init(void) {
  kp_idle_last_activity_ms = k_uptime_get();
  /* The settings load after this and ask again; this is for a keyboard with no
   * saved settings, which nothing asks. */
  k_work_schedule_for_queue(kp_rgb_work_q(), &kp_idle_eval_work, K_SECONDS(2));
  return 0;
}

SYS_INIT(kp_idle_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

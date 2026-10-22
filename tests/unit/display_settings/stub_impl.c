/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fake clock, work queues, display, settings store and activity/USB state for the host
 * test of src/display_settings.c.
 */
#include "stub_all.h"

#if defined(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
#include "display_settings.h"
#endif

int g_log_warnings;
int g_log_module_declared;
struct k_work_q g_sys_q = {"sys"};
static struct k_work_q g_disp_q = {"display"};
struct k_work_q *zmk_display_work_q(void) { return &g_disp_q; }

enum zmk_activity_state g_activity = ZMK_ACTIVITY_ACTIVE;
int g_usb_powered;
struct device g_display_dev = {1};

/* ---- clock and work ------------------------------------------------------- */
static int64_t g_now;
int64_t k_uptime_get(void) { return g_now; }

#define MAX_D 16
static struct k_work_delayable *g_tracked[MAX_D];
static int g_ntracked;

static void track(struct k_work_delayable *d) {
  for (int i = 0; i < g_ntracked; i++) {
    if (g_tracked[i] == d) {
      return;
    }
  }
  g_tracked[g_ntracked++] = d;
}

int g_queue_runs_display, g_queue_runs_sys;

int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w) {
  if (q == &g_disp_q) {
    g_queue_runs_display++;
  } else {
    g_queue_runs_sys++;
  }
  w->handler(w);
  return 1;
}

int k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *d,
                              k_timeout_t delay) {
  if (d->pending) {
    return 0;
  }
  d->pending = true;
  d->expires = g_now + delay.ticks;
  d->queue = q;
  track(d);
  return 1;
}

int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *d,
                                k_timeout_t delay) {
  d->pending = true;
  d->expires = g_now + delay.ticks;
  d->queue = q;
  track(d);
  return 1;
}

int k_work_schedule(struct k_work_delayable *d, k_timeout_t delay) {
  return k_work_schedule_for_queue(&g_sys_q, d, delay);
}

int k_work_reschedule(struct k_work_delayable *d, k_timeout_t delay) {
  return k_work_reschedule_for_queue(&g_sys_q, d, delay);
}

int k_work_cancel_delayable(struct k_work_delayable *d) {
  bool was = d->pending;
  d->pending = false;
  return was;
}

bool k_work_cancel_delayable_sync(struct k_work_delayable *d,
                                  struct k_work_sync *sync) {
  (void)sync;
  return k_work_cancel_delayable(d);
}

/* Moves the clock forward, running the delayed work that comes due, in order. */
void sim_advance(int64_t ms) {
  int64_t target = g_now + ms;

  for (;;) {
    struct k_work_delayable *next = NULL;

    for (int i = 0; i < g_ntracked; i++) {
      struct k_work_delayable *d = g_tracked[i];

      if (d->pending && d->expires <= target &&
          (next == NULL || d->expires < next->expires)) {
        next = d;
      }
    }
    if (next == NULL) {
      break;
    }
    if (next->expires > g_now) {
      g_now = next->expires;
    }
    next->pending = false;
    next->work.handler(&next->work);
  }
  g_now = target;
}

int64_t sim_now(void) { return g_now; }

/* ---- display -------------------------------------------------------------- */
int g_screen_on = 1;
int g_contrast = -1;
int g_blank_on_calls, g_blank_off_calls, g_contrast_calls;

int display_blanking_on(const struct device *d) {
  (void)d;
  g_blank_on_calls++;
  g_screen_on = 0;
  return 0;
}

int display_blanking_off(const struct device *d) {
  (void)d;
  g_blank_off_calls++;
  g_screen_on = 1;
  return 0;
}

int display_set_contrast(const struct device *d, uint8_t contrast) {
  (void)d;
  g_contrast_calls++;
  g_contrast = contrast;
  return 0;
}

/* ---- settings store ------------------------------------------------------- */
struct record {
  char key[48];
  int32_t value;
  size_t len;
  bool used;
};
struct record g_store[64];
int g_save_calls, g_delete_calls;

int settings_save_one(const char *name, const void *value, size_t val_len) {
  struct record *free_slot = NULL;

  g_save_calls++;
  for (size_t i = 0; i < sizeof(g_store) / sizeof(g_store[0]); i++) {
    if (g_store[i].used && strcmp(g_store[i].key, name) == 0) {
      memcpy(&g_store[i].value, value, val_len);
      g_store[i].len = val_len;
      return 0;
    }
    if (!g_store[i].used && free_slot == NULL) {
      free_slot = &g_store[i];
    }
  }
  if (free_slot == NULL) {
    return -28;
  }
  snprintf(free_slot->key, sizeof(free_slot->key), "%s", name);
  memcpy(&free_slot->value, value, val_len);
  free_slot->len = val_len;
  free_slot->used = true;
  return 0;
}

int settings_delete(const char *name) {
  g_delete_calls++;
  for (size_t i = 0; i < sizeof(g_store) / sizeof(g_store[0]); i++) {
    if (g_store[i].used && strcmp(g_store[i].key, name) == 0) {
      g_store[i].used = false;
      return 0;
    }
  }
  return 0;
}

int g_kicks;
#if defined(CONFIG_KEYPAW_RGB_SPLIT_SYNC)
void kp_rgb_sync_display_kick(void) { g_kicks++; }
#endif

/* ---- events --------------------------------------------------------------- */
int g_layout_events, g_layout_last_id = -1;

int raise_zmk_display_settings_changed(struct zmk_display_settings_changed ev) {
  g_layout_events++;
  g_layout_last_id = ev.id;
  return 0;
}

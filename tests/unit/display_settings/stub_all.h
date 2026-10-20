/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Minimal host stand-ins for the Zephyr and ZMK APIs used by src/display_settings.c.
 * They exist only to run the logic of that file on a PC with a fake clock; they do not
 * claim to match the real signatures beyond what the file uses. The test that builds
 * them is tests/unit/test_display_settings.py.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* ---- util ----------------------------------------------------------------- */
#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define BIT(n) (1UL << (n))
#define BIT_MASK(n) (BIT(n) - 1UL)
#define BUILD_ASSERT(cond, msg) _Static_assert(cond, msg)

#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)

#define snprintk snprintf

/* ---- log ------------------------------------------------------------------ */
extern int g_log_module_declared;
#define LOG_MODULE_DECLARE(...) extern int g_log_module_declared
extern int g_log_warnings;
#define LOG_WRN(...)                                                           \
  do {                                                                         \
    g_log_warnings++;                                                          \
    printf("    [WRN] ");                                                      \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  } while (0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) LOG_WRN(__VA_ARGS__)

/* ---- atomic --------------------------------------------------------------- */
typedef long atomic_t;
typedef long atomic_val_t;
static inline void atomic_set_bit(atomic_t *t, int bit) { *t |= (1L << bit); }
static inline bool atomic_test_and_clear_bit(atomic_t *t, int bit) {
  bool was = (*t >> bit) & 1;
  *t &= ~(1L << bit);
  return was;
}
static inline void atomic_set(atomic_t *t, atomic_val_t v) { *t = v; }

/* ---- kernel: a fake clock and fake work queues ----------------------------- */
typedef struct {
  int64_t ticks;
} k_timeout_t;
#define K_MSEC(ms) ((k_timeout_t){.ticks = (int64_t)(ms)})
#define K_SECONDS(s) K_MSEC((int64_t)(s) * 1000)
#define K_NO_WAIT ((k_timeout_t){.ticks = 0})
#define K_FOREVER ((k_timeout_t){.ticks = -1})

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);
struct k_work_q {
  const char *name;
};
struct k_work {
  k_work_handler_t handler;
};
struct k_work_delayable {
  struct k_work work;
  bool pending;
  int64_t expires;
  struct k_work_q *queue;
};
struct k_work_sync {
  int unused;
};
struct k_mutex {
  int locked;
};

#define K_WORK_DEFINE(name, h) struct k_work name = {.handler = (h)}
#define K_WORK_DELAYABLE_DEFINE(name, h)                                       \
  struct k_work_delayable name = {.work = {.handler = (h)}}
#define K_MUTEX_DEFINE(name) struct k_mutex name = {0}

extern struct k_work_q g_sys_q;
int64_t k_uptime_get(void);
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w);
int k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *d,
                              k_timeout_t delay);
int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *d,
                                k_timeout_t delay);
int k_work_schedule(struct k_work_delayable *d, k_timeout_t delay);
int k_work_reschedule(struct k_work_delayable *d, k_timeout_t delay);
int k_work_cancel_delayable(struct k_work_delayable *d);
bool k_work_cancel_delayable_sync(struct k_work_delayable *d,
                                  struct k_work_sync *sync);
static inline int k_mutex_lock(struct k_mutex *m, k_timeout_t t) {
  ARG_UNUSED(t);
  m->locked++;
  return 0;
}
static inline int k_mutex_unlock(struct k_mutex *m) {
  m->locked--;
  return 0;
}

/* ---- init / settings / device tree / display ------------------------------- */
#define POST_KERNEL 1
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
extern int (*g_sys_init_fn)(void);
#define SYS_INIT(fn, level, prio) int (*g_sys_init_fn)(void) = fn

struct device {
  int ready;
};
extern struct device g_display_dev;
static inline bool device_is_ready(const struct device *d) { return d->ready; }
#define DT_HAS_CHOSEN(x) 1
#define DT_CHOSEN(x) x
#define DEVICE_DT_GET(x) (&g_display_dev)

int display_blanking_on(const struct device *d);
int display_blanking_off(const struct device *d);
int display_set_contrast(const struct device *d, uint8_t contrast);

typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);
struct settings_handler_stub {
  const char *name;
  int (*h_set)(const char *key, size_t len, settings_read_cb read_cb,
               void *cb_arg);
  int (*h_commit)(void);
};
extern struct settings_handler_stub g_settings_handler;
#define SETTINGS_STATIC_HANDLER_DEFINE(hname, tree, get, set, commit, exp)     \
  struct settings_handler_stub g_settings_handler = {tree, set, commit}
int settings_save_one(const char *name, const void *value, size_t val_len);
int settings_delete(const char *name);

/* ---- ZMK ------------------------------------------------------------------ */
enum zmk_activity_state {
  ZMK_ACTIVITY_ACTIVE,
  ZMK_ACTIVITY_IDLE,
  ZMK_ACTIVITY_SLEEP
};
extern enum zmk_activity_state g_activity;
static inline enum zmk_activity_state zmk_activity_get_state(void) {
  return g_activity;
}
extern int g_usb_powered;
static inline bool zmk_usb_is_powered(void) { return g_usb_powered; }

struct k_work_q *zmk_display_work_q(void);

typedef struct {
  int unused;
} zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
extern int (*g_listener_cb)(const zmk_event_t *);
#define ZMK_LISTENER(name, cb) int (*g_listener_cb)(const zmk_event_t *) = cb
#define ZMK_SUBSCRIPTION(name, ev) extern int g_unused_subscription_##name##ev

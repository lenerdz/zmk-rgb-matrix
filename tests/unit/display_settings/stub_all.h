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
typedef unsigned long atomic_t;
typedef long atomic_val_t;
/* `long` is 32 bits on some hosts: size the words by the type. */
#define ATOMIC_BITS (sizeof(atomic_t) * 8)
#define ATOMIC_BITMAP_SIZE(num_bits) (((num_bits) + ATOMIC_BITS - 1) / ATOMIC_BITS)
#define ATOMIC_DEFINE(name, num_bits) atomic_t name[ATOMIC_BITMAP_SIZE(num_bits)]
static inline void atomic_set_bit(atomic_t *t, int bit) {
  t[bit / ATOMIC_BITS] |= ((atomic_t)1 << (bit % ATOMIC_BITS));
}
static inline bool atomic_test_and_clear_bit(atomic_t *t, int bit) {
  atomic_t mask = (atomic_t)1 << (bit % ATOMIC_BITS);
  bool was = (t[bit / ATOMIC_BITS] & mask) != 0;
  t[bit / ATOMIC_BITS] &= ~mask;
  return was;
}

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

/* ---- spinlock (idle_policy.c) ---------------------------------------------- */
struct k_spinlock {
  int locked;
};
typedef int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {
  l->locked++;
  return 0;
}
static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key) {
  (void)key;
  l->locked--;
}
#define APPLICATION 2
#define CONFIG_APPLICATION_INIT_PRIORITY 90

/* ---- ZMK ------------------------------------------------------------------ */
extern int g_usb_powered;
static inline bool zmk_usb_is_powered(void) { return g_usb_powered; }

struct k_work_q *zmk_display_work_q(void);

/* The events that idle_policy.c listens to. One stand-in carries whichever of them
 * `kind` says (1 a key position, 2 USB), as the as_zmk_*() accessors return it. */
struct zmk_position_state_changed {
  uint8_t source;
  uint32_t position;
  bool state;
};
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL UINT8_MAX
struct zmk_usb_conn_state_changed {
  int conn_state;
};
typedef struct {
  int kind;
  struct zmk_position_state_changed position;
  struct zmk_usb_conn_state_changed usb;
} zmk_event_t;
static inline const struct zmk_position_state_changed *
as_zmk_position_state_changed(const zmk_event_t *eh) {
  return eh->kind == 1 ? &eh->position : NULL;
}
static inline const struct zmk_usb_conn_state_changed *
as_zmk_usb_conn_state_changed(const zmk_event_t *eh) {
  return eh->kind == 2 ? &eh->usb : NULL;
}
#define ZMK_EV_EVENT_BUBBLE 0
extern int (*g_listener_cb)(const zmk_event_t *);
#define ZMK_LISTENER(name, cb) int (*g_listener_cb)(const zmk_event_t *) = cb
#define ZMK_SUBSCRIPTION(name, ev) extern int g_unused_subscription_##name##ev

/* zmk/events/display_settings_changed.h */
struct zmk_display_settings_changed {
  uint8_t id;
};
int raise_zmk_display_settings_changed(struct zmk_display_settings_changed ev);

/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Split RGB state sync: pushes the selected effect (index, colour, period) and
 * the user on/off intent to a peripheral when it newly appears, and again when
 * the active effect changes to one it has not received. The second push covers
 * the effect colour and period: the GLOBAL invocation only carries the index, so
 * without it a peripheral renders a switched-to effect with its own stale
 * parameters. A send that fails is retried with bounded backoff while the
 * peripheral stays present.
 *
 * The same machinery carries the editable colour maps (rgb_maps.c): after the
 * overlays, a last phase resets the peripheral's maps and then sends, one slot at
 * a time, every slot the peripheral paints whose live colour differs from what the
 * peripheral is known to hold. An edit made later queues that phase again.
 *
 * The display settings (display_settings.c) go last, one command per setting, and
 * a change made later queues just that phase again.
 *
 * Central only; the trigger is a poll, as there is no central-side "peripheral
 * connected" event.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/keypaw/rgb_matrix.h>
#include <zmk/behavior.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>

#include "display_settings.h"
#include "rgb_maps.h"
#include "rgb_matrix_internal.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* The maps are synced when there is at least one and the halves share a slot
 * space (rgb_maps.h). */
#if KP_RGB_MAP_COUNT > 0 && KP_RGB_SLOT_COUNT > 0
#define KP_RGB_SYNC_HAS_MAPS 1
#else
#define KP_RGB_SYNC_HAS_MAPS 0
#endif

/* The display settings (display_settings.c) are synced when the module has
 * them. */
#if IS_ENABLED(CONFIG_KEYPAW_DISPLAY_SETTINGS)
#define KP_RGB_SYNC_HAS_DISPLAY 1
#else
#define KP_RGB_SYNC_HAS_DISPLAY 0
#endif

#define KP_RGB_SYNC_SOURCES MAX(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT, 1)
#define KP_RGB_SYNC_NO_EFFECT UINT16_MAX
#define KP_RGB_SYNC_STEP_DELAY_MS 50
#define KP_RGB_SYNC_MAX_BACKOFF_MS 1000
#define KP_RGB_SYNC_BACKOFF_SHIFT_MAX 5

enum kp_rgb_sync_phase {
  KP_RGB_SYNC_SELECT,   /* RGB_EFS_CMD: must precede colour/period */
  KP_RGB_SYNC_COLOR,    /* RGB_COLOR_HSB_CMD on the now-active effect */
  KP_RGB_SYNC_DURATION, /* RGB_SPI_CMD on the now-active effect */
  KP_RGB_SYNC_POWER,    /* RGB_ON_CMD / RGB_OFF_CMD */
  KP_RGB_SYNC_OVERLAYS,
  KP_RGB_SYNC_MAPS, /* colour maps; also queued alone by an edit */
  KP_RGB_SYNC_DISPLAY, /* display settings; also queued alone by a change */
};

enum kp_rgb_sync_emit {
  KP_RGB_SYNC_EMIT_SENT,  /* one command sent; the phase already advanced */
  KP_RGB_SYNC_EMIT_RETRY, /* transient failure; the same command stays armed */
  KP_RGB_SYNC_EMIT_DONE,  /* nothing left to send for this source */
};

struct kp_rgb_sync_source {
  bool seen;           /* present at the last sample */
  bool pending;        /* a sync is in flight for this source */
  uint8_t retry_count; /* consecutive failed sends for the in-flight command */
  uint16_t mask_word;  /* next overlay word to push this time round */
  int64_t ready_at;
  enum kp_rgb_sync_phase phase;
  /* Last effect fully pushed to this source; KP_RGB_SYNC_NO_EFFECT until one
   * completes. A change of active effect away from it re-queues a push. */
  uint16_t synced_effect;
#if KP_RGB_SYNC_HAS_DISPLAY
  /* The next display setting to push in the display phase. */
  uint8_t display_index;
#endif
  /* Snapshot taken under kp_rgb_matrix_lock() at KP_RGB_SYNC_SELECT. */
  uint16_t effect_index;
  uint16_t duration_ms;
  struct kp_rgb_hsb color;
  bool user_on;
#if KP_RGB_SYNC_HAS_MAPS
  /* False until the source's maps have been reset in this connection, which
   * makes `shadow` (below) the defaults. */
  bool maps_primed;
  /* The colour the source is known to hold for every slot of every map, laid out
   * like kp_rgb_maps_fill_defaults() fills it. */
  uint32_t shadow[KP_RGB_MAP_COUNT * KP_RGB_SLOT_CAP];
#endif
};

static struct k_work_delayable kp_rgb_sync_poll;
static struct k_work_delayable kp_rgb_sync_step;
static struct k_work_delayable kp_rgb_sync_clock;
static struct kp_rgb_sync_source kp_rgb_sync_sources[KP_RGB_SYNC_SOURCES];
static const struct zmk_split_transport_central *kp_rgb_sync_transport;
#if KP_RGB_SYNC_HAS_MAPS
static struct k_work kp_rgb_sync_maps_work;
static volatile bool kp_rgb_sync_maps_ready;
#endif
#if KP_RGB_SYNC_HAS_DISPLAY
static struct k_work kp_rgb_sync_display_work;
static volatile bool kp_rgb_sync_display_ready;
#endif

/* Registration order is priority order, and a NULL get_status means "always
 * available". active_transport is private to the split central, so the transport
 * has to be re-derived here. */
static const struct zmk_split_transport_central *kp_rgb_sync_pick_transport(void) {
  STRUCT_SECTION_FOREACH(zmk_split_transport_central, t) {
    if (t->api == NULL || t->api->send_command == NULL ||
        t->api->get_available_source_ids == NULL) {
      continue;
    }
    if (t->api->get_status != NULL && !t->api->get_status().available) {
      continue;
    }
    return t;
  }
  return NULL;
}

/* Samples the connected set, queues a sync for every source that newly appeared
 * or whose active effect changed to one it has not received, and abandons one
 * that disappeared. Called from both handlers. */
static void kp_rgb_sync_refresh(void) {
  const struct zmk_split_transport_central *t = kp_rgb_sync_pick_transport();

  if (t != kp_rgb_sync_transport) {
    /* A different transport now owns the source ids, so what was seen before
     * says nothing about them; force every source to re-sync. */
    kp_rgb_sync_transport = t;
    for (size_t i = 0; i < ARRAY_SIZE(kp_rgb_sync_sources); i++) {
      kp_rgb_sync_sources[i].seen = false;
      kp_rgb_sync_sources[i].pending = false;
      kp_rgb_sync_sources[i].retry_count = 0;
      kp_rgb_sync_sources[i].synced_effect = KP_RGB_SYNC_NO_EFFECT;
#if KP_RGB_SYNC_HAS_MAPS
      kp_rgb_sync_sources[i].maps_primed = false;
#endif
    }
  }

  bool present[KP_RGB_SYNC_SOURCES] = {false};
  if (t != NULL) {
    uint8_t ids[KP_RGB_SYNC_SOURCES];
    int count = t->api->get_available_source_ids(ids);
    if (count < 0) {
      LOG_WRN("Source query failed (err %d)", count);
      count = 0;
    }
    for (int i = 0; i < count; i++) {
      if (ids[i] < ARRAY_SIZE(present)) {
        present[ids[i]] = true;
      }
    }
  }

  kp_rgb_matrix_lock();
  uint16_t active = (uint16_t)kp_rgb_controller.effect_index;
  kp_rgb_matrix_unlock();

  int64_t now = k_uptime_get();
  for (size_t s = 0; s < ARRAY_SIZE(kp_rgb_sync_sources); s++) {
    struct kp_rgb_sync_source *st = &kp_rgb_sync_sources[s];
    if (present[s] && !st->seen) {
      st->seen = true;
      st->pending = true;
      st->retry_count = 0;
      st->phase = KP_RGB_SYNC_SELECT;
      st->mask_word = 0;
      st->synced_effect = KP_RGB_SYNC_NO_EFFECT;
#if KP_RGB_SYNC_HAS_MAPS
      st->maps_primed = false; /* its maps are reset before anything is pushed */
#endif
      /* A peripheral is marked connected before its GATT characteristics are
       * discovered, and the split worker silently drops commands in that
       * window. */
      st->ready_at = now + CONFIG_KEYPAW_RGB_SPLIT_SYNC_SETTLE_MS;
      LOG_INF("Queued for source %u", (uint32_t)s);
      /* Align the shared clock once discovery has settled as well. */
      k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_clock,
                                  K_MSEC(CONFIG_KEYPAW_RGB_SPLIT_SYNC_SETTLE_MS));
    } else if (!present[s] && st->seen) {
      st->seen = false;
      st->pending = false; /* abort in flight; a reconnect re-syncs */
      st->synced_effect = KP_RGB_SYNC_NO_EFFECT;
#if KP_RGB_SYNC_HAS_MAPS
      st->maps_primed = false; /* it may have rebooted */
#endif
    } else if (present[s] && st->seen && !st->pending &&
               st->synced_effect != active) {
      /* Active effect changed to one this source has not received. */
      st->pending = true;
      st->retry_count = 0;
      st->phase = KP_RGB_SYNC_SELECT;
      st->mask_word = 0;
      st->ready_at = now;
      LOG_INF("Effect change queued for source %u", (uint32_t)s);
    }
  }
}

/* Absolute commands only: the peripheral does not run the central-state
 * conversion pass, so a relative command would be applied against whatever the
 * peripheral already had. */
static bool kp_rgb_sync_send(uint8_t source, uint32_t cmd, uint32_t arg) {
  struct zmk_behavior_binding binding = {
      .behavior_dev = kp_rgb_controller.dev->name,
      .param1 = cmd,
      .param2 = arg,
  };
  /* The position is truncated to a byte on the wire. The peripheral handler
   * reads it only to tell this push from a key press (see
   * KP_RGB_SYNC_EVENT_POSITION), so the change event is not raised for it.
   * state=true calls the pressed handler. */
  struct zmk_behavior_binding_event event = {
      .layer = 0,
      .position = KP_RGB_SYNC_EVENT_POSITION,
      .timestamp = k_uptime_get(),
      .source = source,
  };

  int err = zmk_split_central_invoke_behavior(source, &binding, event, true);
  if (err < 0) {
    LOG_WRN("Source %u command %u failed (err %d)", source, cmd, err);
    return false;
  }
  return true;
}

/* Pushes the central's shared animation clock to every connected source. Kept
 * out of the snapshot state machine so it keeps running while a snapshot is
 * stalled or retrying; a dropped send self-heals on the next period. */
static void kp_rgb_sync_send_clock(void) {
  const struct zmk_split_transport_central *t = kp_rgb_sync_pick_transport();
  if (t == NULL) {
    return;
  }
  uint8_t ids[KP_RGB_SYNC_SOURCES];
  int count = t->api->get_available_source_ids(ids);
  if (count < 0) {
    return;
  }
  uint32_t now = (uint32_t)k_uptime_get();
  for (int i = 0; i < count; i++) {
    if (ids[i] < KP_RGB_SYNC_SOURCES) {
      (void)kp_rgb_sync_send(ids[i], RGB_CLOCK_CMD, now);
    }
  }
}

static void kp_rgb_sync_clock_handler(struct k_work *work) {
  ARG_UNUSED(work);
  kp_rgb_sync_send_clock();
  k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_clock,
                              K_MSEC(CONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS));
}

#if KP_RGB_SYNC_HAS_MAPS
/* The maps phase: reset the source's maps once per connection, then send the
 * slots that differ, one per call. The live colours are read afresh on every
 * call, so an edit made while this runs is picked up. */
static enum kp_rgb_sync_emit kp_rgb_sync_emit_maps(uint8_t source,
                                                   struct kp_rgb_sync_source *st) {
  if (!st->maps_primed) {
    if (!kp_rgb_sync_send(source, RGB_MAP_PARAM1(RGB_MAP_RESET_CMD, RGB_MAP_ALL),
                          0)) {
      return KP_RGB_SYNC_EMIT_RETRY;
    }
    kp_rgb_maps_fill_defaults(st->shadow);
    st->maps_primed = true;
    return KP_RGB_SYNC_EMIT_SENT;
  }

  size_t map, slot;
  uint32_t rgb;

  if (kp_rgb_maps_next_peer_diff(st->shadow, &map, &slot, &rgb)) {
    if (!kp_rgb_sync_send(source,
                          RGB_MAP_PARAM1(RGB_MAP_SET_CMD, kp_rgb_map_layer_id(map)),
                          RGB_MAP_SET_VAL(slot, rgb))) {
      return KP_RGB_SYNC_EMIT_RETRY;
    }
    st->shadow[map * KP_RGB_SLOT_CAP + slot] = rgb;
    return KP_RGB_SYNC_EMIT_SENT;
  }

#if KP_RGB_SYNC_HAS_DISPLAY
  st->phase = KP_RGB_SYNC_DISPLAY;
  st->display_index = 0;
  return KP_RGB_SYNC_EMIT_SENT;
#else
  LOG_INF("Complete for source %u", source);
  return KP_RGB_SYNC_EMIT_DONE;
#endif
}
#endif /* KP_RGB_SYNC_HAS_MAPS */

#if KP_RGB_SYNC_HAS_DISPLAY
/* The display phase: every display setting, one per call. The values are read
 * afresh on every call. */
static enum kp_rgb_sync_emit kp_rgb_sync_emit_display(uint8_t source,
                                                      struct kp_rgb_sync_source *st) {
  uint32_t param1, param2;

  if (kp_display_sync_get(st->display_index, &param1, &param2)) {
    if (!kp_rgb_sync_send(source, param1, param2)) {
      return KP_RGB_SYNC_EMIT_RETRY;
    }
    st->display_index++;
    return KP_RGB_SYNC_EMIT_SENT;
  }

  LOG_INF("Complete for source %u", source);
  return KP_RGB_SYNC_EMIT_DONE;
}
#endif /* KP_RGB_SYNC_HAS_DISPLAY */

/* Emits at most one wire command. */
static enum kp_rgb_sync_emit kp_rgb_sync_emit_one(uint8_t source,
                                                  struct kp_rgb_sync_source *st) {
#if KP_RGB_SYNC_HAS_MAPS
  if (st->phase == KP_RGB_SYNC_MAPS) {
    return kp_rgb_sync_emit_maps(source, st);
  }
#endif
#if KP_RGB_SYNC_HAS_DISPLAY
  if (st->phase == KP_RGB_SYNC_DISPLAY) {
    return kp_rgb_sync_emit_display(source, st);
  }
#endif
  if (st->phase != KP_RGB_SYNC_OVERLAYS) {
    uint32_t cmd, arg;
    switch (st->phase) {
    case KP_RGB_SYNC_SELECT: {
      kp_rgb_matrix_lock();
      st->effect_index = (uint16_t)kp_rgb_controller.effect_index;
      st->user_on = kp_rgb_controller.state.user_on;
      const struct device *fx = kp_rgb_controller.state.active_fx;
      if (fx != NULL) {
        const struct kp_rgb_effect_common_data *data = kp_rgb_effect_data(fx);
        st->color = data->color;
        /* The brightness is the controller's, and the handler of
         * RGB_COLOR_HSB_CMD applies the `b` it carries to it. */
        st->color.b = kp_rgb_controller.brightness;
        /* Zero means "increase" in RGB_SPI_CMD, not an absolute period. */
        st->duration_ms = (uint16_t)MAX(data->duration_ms, 1u);
      }
      kp_rgb_matrix_unlock();
      if (fx == NULL) {
        st->phase = KP_RGB_SYNC_POWER;
        return KP_RGB_SYNC_EMIT_SENT;
      }
      cmd = RGB_EFS_CMD;
      arg = st->effect_index;
      break;
    }
    case KP_RGB_SYNC_COLOR:
      cmd = RGB_COLOR_HSB_CMD;
      arg = RGB_COLOR_HSB_VAL(st->color.h, st->color.s, st->color.b);
      break;
    case KP_RGB_SYNC_DURATION:
      cmd = RGB_SPI_CMD;
      arg = st->duration_ms;
      break;
    case KP_RGB_SYNC_POWER:
      cmd = st->user_on ? RGB_ON_CMD : RGB_OFF_CMD;
      arg = 0;
      break;
    default:
      return KP_RGB_SYNC_EMIT_DONE;
    }

    if (!kp_rgb_sync_send(source, cmd, arg)) {
      return KP_RGB_SYNC_EMIT_RETRY;
    }
    st->phase++;
    return KP_RGB_SYNC_EMIT_SENT;
  }

  if (st->mask_word < kp_rgb_overlay_word_count()) {
    uint16_t word = st->mask_word;
    if (!kp_rgb_sync_send(source, RGB_OVL_STATE_CMD,
                          RGB_OVL_STATE_VAL(word, kp_rgb_overlay_get_word(word)))) {
      return KP_RGB_SYNC_EMIT_RETRY;
    }
    st->mask_word++;
    return KP_RGB_SYNC_EMIT_SENT;
  }

#if KP_RGB_SYNC_HAS_MAPS
  st->phase = KP_RGB_SYNC_MAPS;
  return KP_RGB_SYNC_EMIT_SENT;
#elif KP_RGB_SYNC_HAS_DISPLAY
  st->phase = KP_RGB_SYNC_DISPLAY;
  st->display_index = 0;
  return KP_RGB_SYNC_EMIT_SENT;
#else
  LOG_INF("Complete for source %u", source);
  return KP_RGB_SYNC_EMIT_DONE;
#endif
}

#if KP_RGB_SYNC_HAS_MAPS
/* Runs on the module work queue, like every other handler here, so it cannot
 * interleave with a step: queues the maps phase for every source that is idle. A
 * source that is busy reaches the phase on its own, and reads the live colours
 * only then. */
static void kp_rgb_sync_maps_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  int64_t now = k_uptime_get();
  bool queued = false;

  for (size_t s = 0; s < ARRAY_SIZE(kp_rgb_sync_sources); s++) {
    struct kp_rgb_sync_source *st = &kp_rgb_sync_sources[s];

    if (st->seen && !st->pending) {
      st->pending = true;
      st->retry_count = 0;
      st->phase = KP_RGB_SYNC_MAPS;
      st->ready_at = now;
    }
    queued = queued || st->pending;
  }
  if (queued) {
    k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_step, K_NO_WAIT);
  }
}

void kp_rgb_sync_maps_kick(void) {
  if (kp_rgb_sync_maps_ready) {
    k_work_submit_to_queue(kp_rgb_work_q(), &kp_rgb_sync_maps_work);
  }
}
#endif /* KP_RGB_SYNC_HAS_MAPS */

#if KP_RGB_SYNC_HAS_DISPLAY
/* Queues the display phase for every source that is idle. A source in the
 * middle of the phase starts it again, because a setting it already passed may
 * have changed; one in an earlier phase reaches the phase on its own and reads
 * the live values only then. Runs on the module work queue, like every other
 * handler here, so it cannot interleave with a step. */
static void kp_rgb_sync_display_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  int64_t now = k_uptime_get();
  bool queued = false;

  for (size_t s = 0; s < ARRAY_SIZE(kp_rgb_sync_sources); s++) {
    struct kp_rgb_sync_source *st = &kp_rgb_sync_sources[s];

    if (st->seen && !st->pending) {
      st->pending = true;
      st->retry_count = 0;
      st->phase = KP_RGB_SYNC_DISPLAY;
      st->display_index = 0;
      st->ready_at = now;
    } else if (st->seen && st->phase == KP_RGB_SYNC_DISPLAY) {
      st->display_index = 0;
    }
    queued = queued || st->pending;
  }
  if (queued) {
    k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_step, K_NO_WAIT);
  }
}

void kp_rgb_sync_display_kick(void) {
  if (kp_rgb_sync_display_ready) {
    k_work_submit_to_queue(kp_rgb_work_q(), &kp_rgb_sync_display_work);
  }
}
#endif /* KP_RGB_SYNC_HAS_DISPLAY */

static void kp_rgb_sync_step_handler(struct k_work *work) {
  ARG_UNUSED(work);
  kp_rgb_sync_refresh(); /* aborts vanished sources, queues new ones */

  int64_t now = k_uptime_get();
  int64_t next_ms = -1;

  for (size_t s = 0; s < ARRAY_SIZE(kp_rgb_sync_sources); s++) {
    struct kp_rgb_sync_source *st = &kp_rgb_sync_sources[s];
    if (!st->pending) {
      continue;
    }
    if (now < st->ready_at) {
      int64_t wait = st->ready_at - now;
      if (next_ms < 0 || wait < next_ms) {
        next_ms = wait;
      }
      continue;
    }
    enum kp_rgb_sync_emit r = kp_rgb_sync_emit_one((uint8_t)s, st);
    if (r == KP_RGB_SYNC_EMIT_SENT) {
      st->retry_count = 0;
      next_ms = KP_RGB_SYNC_STEP_DELAY_MS;
      break; /* one command per work item, then yield */
    }
    if (r == KP_RGB_SYNC_EMIT_RETRY) {
      uint8_t shift = MIN(st->retry_count, KP_RGB_SYNC_BACKOFF_SHIFT_MAX);
      int64_t backoff = MIN((int64_t)(KP_RGB_SYNC_STEP_DELAY_MS << shift),
                            (int64_t)KP_RGB_SYNC_MAX_BACKOFF_MS);
      st->retry_count = MIN(st->retry_count + 1, KP_RGB_SYNC_BACKOFF_SHIFT_MAX);
      st->ready_at = now + backoff;
      if (next_ms < 0 || backoff < next_ms) {
        next_ms = backoff;
      }
      continue; /* other sources still make progress */
    }
    st->pending = false;
    st->synced_effect = st->effect_index;
  }

  if (next_ms >= 0) {
    k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_step,
                                K_MSEC((uint32_t)next_ms));
  }
}

static void kp_rgb_sync_poll_handler(struct k_work *work) {
  ARG_UNUSED(work);
  kp_rgb_sync_refresh();

  for (size_t s = 0; s < ARRAY_SIZE(kp_rgb_sync_sources); s++) {
    if (kp_rgb_sync_sources[s].pending) {
      k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_step,
                                  K_NO_WAIT);
      break;
    }
  }

  k_work_reschedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_poll,
                              K_MSEC(CONFIG_KEYPAW_RGB_SPLIT_SYNC_POLL_MS));
}

static int kp_rgb_split_sync_init(void) {
  k_work_init_delayable(&kp_rgb_sync_poll, kp_rgb_sync_poll_handler);
  k_work_init_delayable(&kp_rgb_sync_step, kp_rgb_sync_step_handler);
  k_work_init_delayable(&kp_rgb_sync_clock, kp_rgb_sync_clock_handler);
#if KP_RGB_SYNC_HAS_MAPS
  k_work_init(&kp_rgb_sync_maps_work, kp_rgb_sync_maps_work_handler);
  kp_rgb_sync_maps_ready = true;
#endif
#if KP_RGB_SYNC_HAS_DISPLAY
  k_work_init(&kp_rgb_sync_display_work, kp_rgb_sync_display_work_handler);
  kp_rgb_sync_display_ready = true;
#endif
  /* The module work queue is started at POST_KERNEL, well before this. The
   * first poll is delayed rather than immediate to stay clear of the
   * settings_load() that runs at the top of main(). */
  k_work_schedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_poll,
                            K_MSEC(CONFIG_KEYPAW_RGB_SPLIT_SYNC_POLL_MS));
  k_work_schedule_for_queue(kp_rgb_work_q(), &kp_rgb_sync_clock,
                            K_MSEC(CONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS));
  return 0;
}
SYS_INIT(kp_rgb_split_sync_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

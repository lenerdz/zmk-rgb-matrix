#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Production zmk_rgb_matrix_changed announcements and the brightness of RGB_COLOR_HSB_CMD.

Compiles the real command handler of src/behavior_rgb_matrix.c against the mocks of
test_parameter_controls.py plus stand-ins for what the handler reaches outside the file,
and the real event header (include/zmk/events/rgb_matrix_changed.h) against a stub of
the ZMK event macros that records what is raised.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_parameter_controls import MOCKS, ROOT, SOURCE

PRELUDE = r'''
#define IS_ENABLED(option) option
#define CONFIG_KEYPAW_RGB_MATRIX_CHANGE_EVENT 1
'''

# What on_keymap_binding_pressed() reaches that lives in other files.
EXTRA = r'''
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define KP_RGB_SYNC_EVENT_POSITION 0xFFu
#define RGB_OVL_STATE_CMD 0x101
#define RGB_RESET_CMD 0x102
#define RGB_CLOCK_CMD 0x103
#define RGB_MAP_IS_CMD(p) ((p) >= 0x104 && (p) <= 0x105)
#define RGB_OVL_STATE_WORD(param) ((uint16_t)((param) >> 16))
#define RGB_OVL_STATE_BITS(param) ((uint16_t)(param))
#include <zmk/events/rgb_matrix_changed.h>

static unsigned saves, resets, internal_commands;
static int kp_rgb_save_state(void) { saves++; return 0; }
static void kp_rgb_reset_state(void) {
    resets++;
    kp_rgb_matrix_lock();
    kp_rgb_controller.brightness = 100;
    kp_rgb_controller.state.user_on = true;
    kp_rgb_matrix_unlock();
}
static int kp_rgb_map_command(uint32_t p1, uint32_t p2) {
    (void)p1; (void)p2; internal_commands++; return 0;
}
static bool kp_rgb_overlay_set_word(uint16_t word, uint16_t bits) {
    (void)word; (void)bits; internal_commands++; return true;
}
static void kp_rgb_set_clock_offset(int32_t offset) { (void)offset; internal_commands++; }
static int64_t k_uptime_get(void) { return 12345; }
static int zmk_rgb_matrix_on(void) {
    kp_rgb_matrix_lock(); kp_rgb_controller.state.user_on = true; kp_rgb_matrix_unlock();
    return 0;
}
static int zmk_rgb_matrix_off(void) {
    kp_rgb_matrix_lock(); kp_rgb_controller.state.user_on = false; kp_rgb_matrix_unlock();
    return 0;
}
static int zmk_rgb_matrix_toggle(void) {
    kp_rgb_matrix_lock();
    kp_rgb_controller.state.user_on = !kp_rgb_controller.state.user_on;
    kp_rgb_matrix_unlock();
    return 0;
}
'''

STUB_EVENT_MANAGER = r'''
#pragma once
#include <stdint.h>
/* Records the last raise and how many there were; the trailing typedef consumes
 * the semicolon that follows ZMK_EVENT_DECLARE(...) in the real header. */
#define ZMK_EVENT_DECLARE(name)                                                \
  static unsigned name##_raised;                                               \
  static struct name name##_last;                                              \
  static inline int raise_##name(struct name data) {                           \
    name##_last = data;                                                        \
    name##_raised++;                                                           \
    return 0;                                                                  \
  }                                                                            \
  typedef int name##_stub_t
'''

TESTS = r'''
#define RAISED zmk_rgb_matrix_changed_raised
#define LAST zmk_rgb_matrix_changed_last

static struct device fx[3] = {
    {.ready = true, .name = "fx_solid"},
    {.ready = true, .name = "fx_rainbow"},
    {.ready = true, .name = "plain"},
};
static const struct device *const all_fx[] = {&fx[0], &fx[1], &fx[2]};

static int press(uint32_t cmd, uint32_t arg, uint32_t position) {
    struct zmk_behavior_binding binding = {.param1 = cmd, .param2 = arg};
    struct zmk_behavior_binding_event event = {.position = position};
    assert(!locked);
    return on_keymap_binding_pressed(&binding, event);
}

int main(void) {
    kp_rgb_controller.effects = all_fx;
    kp_rgb_controller.effect_count = 3;
    kp_rgb_controller.effect_index = 0;
    kp_rgb_controller.state.active_fx = &fx[0];
    kp_rgb_controller.state.user_on = true;
    kp_rgb_controller.brightness = 50;
    for (int i = 0; i < 3; i++) {
        fx[i].data.color = (struct kp_rgb_hsb){100, 80, 100};
        fx[i].data.duration_ms = 1000;
    }

    /* Brightness, as a raw command (a peripheral or a standalone board). */
    assert(press(RGB_BRI_CMD, 0, 7) == 0);
    assert(RAISED == 1 && saves == 1);
    assert(LAST.kind == ZMK_RGB_MATRIX_CHANGE_BRIGHTNESS && LAST.brightness == 60);
    assert(LAST.on && LAST.effect == 0 && LAST.effect_count == 3);
    assert(LAST.effect_name != NULL && !strcmp(LAST.effect_name, "solid"));
    assert(LAST.hue == 100 && LAST.saturation == 80 && LAST.period_ms == 1000);

    /* The converted form: the central hands the absolute colour, and its `b`
     * is the controller's brightness. */
    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(100, 80, 30), 7) == 0);
    assert(kp_rgb_controller.brightness == 30);
    assert(RAISED == 2 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_BRIGHTNESS && LAST.brightness == 30);

    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(140, 80, 30), 7) == 0);
    assert(RAISED == 3 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_HUE && LAST.hue == 140);

    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(140, 55, 30), 7) == 0);
    assert(RAISED == 4 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_SATURATION && LAST.saturation == 55);

    /* The same colour again changes nothing: saved, but not announced. */
    unsigned s = saves;
    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(140, 55, 30), 7) == 0);
    assert(RAISED == 4 && saves == s + 1);

    /* An invalid brightness is rejected before anything changes. */
    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(140, 55, 101), 7) < 0);
    assert(RAISED == 4 && saves == s + 1 && kp_rgb_controller.brightness == 30);

    /* The animation period. */
    assert(press(RGB_SPI_CMD, 2000, 7) == 0);
    assert(RAISED == 5 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_PERIOD && LAST.period_ms == 2000);

    /* Effects: the "fx_" prefix of the node name is dropped, another name is kept. */
    assert(press(RGB_EFS_CMD, 1, 7) == 0);
    assert(RAISED == 6 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_EFFECT && LAST.effect == 1);
    assert(!strcmp(LAST.effect_name, "rainbow"));
    assert(press(RGB_EFS_CMD, 1, 7) == 0 && RAISED == 6);
    assert(press(RGB_EFS_CMD, 2, 7) == 0);
    assert(RAISED == 7 && LAST.effect == 2 && !strcmp(LAST.effect_name, "plain"));
    s = saves;
    assert(press(RGB_EFS_CMD, 99, 7) < 0 && RAISED == 7 && saves == s);

    /* Power. */
    assert(press(RGB_OFF_CMD, 0, 7) == 0);
    assert(RAISED == 8 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_POWER && !LAST.on);
    assert(press(RGB_OFF_CMD, 0, 7) == 0 && RAISED == 8);
    assert(press(RGB_ON_CMD, 0, 7) == 0);
    assert(RAISED == 9 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_POWER && LAST.on);
    assert(press(RGB_TOG_CMD, 0, 7) == 0);
    assert(RAISED == 10 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_POWER && !LAST.on);
    assert(press(RGB_ON_CMD, 0, 7) == 0 && RAISED == 11);

    /* What the central pushes to a peripheral applies but is not announced. */
    assert(press(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(200, 55, 80), KP_RGB_SYNC_EVENT_POSITION) == 0);
    assert(kp_rgb_controller.brightness == 80 && fx[2].data.color.h == 200);
    assert(press(RGB_EFS_CMD, 0, KP_RGB_SYNC_EVENT_POSITION) == 0);
    assert(kp_rgb_controller.effect_index == 0 && RAISED == 11);

    /* A key at position 0 is still a key. */
    assert(press(RGB_EFS_CMD, 1, 0) == 0 && RAISED == 12 && LAST.effect == 1);

    /* Reset announces once, and not when it is a push. */
    s = saves;
    assert(press(RGB_RESET_CMD, 0, 3) == 0);
    assert(resets == 1 && saves == s && RAISED == 13);
    assert(LAST.kind == ZMK_RGB_MATRIX_CHANGE_RESET && LAST.brightness == 100 && LAST.on);
    assert(press(RGB_RESET_CMD, 0, KP_RGB_SYNC_EVENT_POSITION) == 0);
    assert(resets == 2 && RAISED == 13);

    /* Internal commands are neither saved nor announced. */
    assert(press(RGB_OVL_STATE_CMD, 0x00010002, 3) == 0);
    assert(press(RGB_CLOCK_CMD, 1000, 3) == 0);
    assert(press(0x104, 0, 3) == 0 && press(0x105, 0, 3) == 0);
    assert(internal_commands == 4 && RAISED == 13 && saves == s);
    assert(press(0x7FFF, 0, 3) == -ENOTSUP && RAISED == 13);

    /* The path of a key press on the central: resolve the relative command to an
     * absolute one, then apply it. The brightness used to be lost on this path. */
    {
        struct zmk_behavior_binding_event ev = {.position = 5};
        struct zmk_behavior_binding b = {.param1 = RGB_BRD_CMD};
        kp_rgb_controller.brightness = 30;
        unsigned r = RAISED;
        assert(on_keymap_binding_convert_central_state_dependent_params(&b, ev) == 0);
        assert(b.param1 == RGB_COLOR_HSB_CMD);
        assert(on_keymap_binding_pressed(&b, ev) == 0);
        assert(kp_rgb_controller.brightness == 20);
        assert(RAISED == r + 1 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_BRIGHTNESS);
        assert(LAST.brightness == 20);

        b = (struct zmk_behavior_binding){.param1 = RGB_BRI_CMD};
        assert(on_keymap_binding_convert_central_state_dependent_params(&b, ev) == 0);
        assert(on_keymap_binding_pressed(&b, ev) == 0);
        assert(kp_rgb_controller.brightness == 30);
        assert(RAISED == r + 2 && LAST.brightness == 30);

        b = (struct zmk_behavior_binding){.param1 = RGB_HUI_CMD};
        assert(on_keymap_binding_convert_central_state_dependent_params(&b, ev) == 0);
        assert(on_keymap_binding_pressed(&b, ev) == 0);
        assert(kp_rgb_controller.brightness == 30);
        assert(RAISED == r + 3 && LAST.kind == ZMK_RGB_MATRIX_CHANGE_HUE);
    }
    (void)embedded_fx;

    assert(!locked);
    return 0;
}
'''


def main():
    text = SOURCE.read_text()
    production = text[text.index("size_t kp_rgb_effect_count("):
                      text.index("int kp_rgb_effect_convert_central_state_dependent_params(")]
    production += text[text.index("static int on_keymap_binding_convert_central_state_dependent_params("):
                       text.index("static int on_keymap_binding_released(")]
    with tempfile.TemporaryDirectory() as tmp:
        stubs = Path(tmp) / "stubs"
        (stubs / "zephyr").mkdir(parents=True)
        (stubs / "zmk").mkdir()
        (stubs / "zephyr" / "kernel.h").write_text("#pragma once\n")
        (stubs / "zmk" / "event_manager.h").write_text(STUB_EVENT_MANAGER)
        source = Path(tmp) / "change_event.c"
        binary = Path(tmp) / "change_event"
        source.write_text(PRELUDE + MOCKS + EXTRA + production + TESTS)
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-missing-field-initializers",
                        "-I", str(stubs), "-I", str(ROOT / "include"),
                        str(source), "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary)], check=True)
    print("change event: passed")


if __name__ == "__main__":
    main()

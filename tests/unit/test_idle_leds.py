#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Production RGB engine with CONFIG_KEYPAW_DISPLAY_SETTINGS: the LEDs follow the idle policy.

With the display settings the strip is off when the user turned it off or when every
group of LEDs it has (the LEDs under the keys, the underglow) was turned off by the idle
policy (src/idle_policy.c, tested apart: test_idle_policy.py); the groups have their own
limits, so one can be dark while the other is lit. ZMK's own idle state is not looked at.
The functions are the production ones of src/rgb_matrix.c; the policy is a stand-in whose
answers the test sets (host_keys_off, host_glow_off) and the strip has an underglow LED
(0) and a LED under a key (1). It checks:

  - the truth table of "the strip is on": the user's switch, the groups the strip has and
    which of them are off; a strip with no LED at all is on when the user says so;
  - the masks: the LEDs of a group that is off show black, the others are not touched;
  - the whole path to the strip: one group dark, both dark (the power goes off and a
    black frame is written), and back, with the colours coming back as they were;
  - that ZMK going idle neither turns the strip off nor makes the effects see the dimmer
    idle brightness.
"""
from engine_harness import run_tests
from test_engine_scheduler import FIXTURE

TESTS = r'''
static void set_map(bool led0_underglow, bool led1_underglow) {
    if (KP_LED_COUNT > 0) kp_map[0] = led0_underglow ? KP_RGB_NO_KEY_XY_FLAG : 0;
    if (KP_LED_COUNT > 1) kp_map[1] = led1_underglow ? KP_RGB_NO_KEY_XY_FLAG : 0;
}
static const struct led_rgb *last_frame(void) {
    assert(host_transfer_count);
    return host_transfers[host_transfer_count - 1].pixels;
}
static void policy_changed(bool keys_off, bool glow_off) {
    host_keys_off = keys_off; host_glow_off = glow_off;
    kp_rgb_idle_changed(); host_run_ready();
}
int main(void) {
    fixture(true, false);

    if (!KP_LED_COUNT) {
        /* a strip with no LED has no group: it is on when the user says so */
        host_keys_off = host_glow_off = true;
        assert(kp_rgb_logical_on_locked());
        ctx.state.user_on = false; assert(!kp_rgb_logical_on_locked());
        puts("idle LEDs: a strip with no LED follows the user's switch only");
        return 0;
    }

    /* truth table: LED 0 is underglow, LED 1 is under a key */
    set_map(true, false);
    ctx.state.user_on = true;
    host_keys_off = false; host_glow_off = false; assert(kp_rgb_logical_on_locked());
    host_keys_off = true;  host_glow_off = false; assert(kp_rgb_logical_on_locked());
    host_keys_off = false; host_glow_off = true;  assert(kp_rgb_logical_on_locked());
    host_keys_off = true;  host_glow_off = true;  assert(!kp_rgb_logical_on_locked());
    ctx.state.user_on = false;
    host_keys_off = false; host_glow_off = false; assert(!kp_rgb_logical_on_locked());
    ctx.state.user_on = true;
    /* a strip with LEDs under the keys only does not care about the underglow */
    set_map(false, false);
    host_keys_off = false; host_glow_off = true;  assert(kp_rgb_logical_on_locked());
    host_keys_off = true;  host_glow_off = false; assert(!kp_rgb_logical_on_locked());
    /* nor one with underglow only about the keys */
    set_map(true, true);
    host_keys_off = true;  host_glow_off = false; assert(kp_rgb_logical_on_locked());
    host_keys_off = false; host_glow_off = true;  assert(!kp_rgb_logical_on_locked());

    /* the masks black out one group, and leave the other alone */
    set_map(true, false);
    {
        struct led_rgb px[2] = {{10, 20, 30}, {40, 50, 60}};
        host_keys_off = false; host_glow_off = false;
        kp_rgb_apply_idle_masks(px);
        assert(px[0].r == 10 && px[0].g == 20 && px[0].b == 30);
        assert(px[1].r == 40 && px[1].g == 50 && px[1].b == 60);
        host_keys_off = true;
        kp_rgb_apply_idle_masks(px);
        assert(px[0].r == 10 && px[0].g == 20 && px[0].b == 30);
        assert(!px[1].r && !px[1].g && !px[1].b);
        px[1] = (struct led_rgb){40, 50, 60};
        host_keys_off = false; host_glow_off = true;
        kp_rgb_apply_idle_masks(px);
        assert(!px[0].r && !px[0].g && !px[0].b);
        assert(px[1].r == 40 && px[1].g == 50 && px[1].b == 60);
        host_keys_off = true;
        px[0] = (struct led_rgb){10, 20, 30};
        kp_rgb_apply_idle_masks(px);
        assert(!px[0].r && !px[1].g && !px[1].b);
    }

    /* the whole path, from the policy to the strip */
    host_keys_off = false; host_glow_off = false;
    host_run_until(40);
    assert(last_frame()[0].r == 17 && last_frame()[1].r == 17);

    policy_changed(true, false); /* the keys go dark, the underglow stays */
    assert(last_frame()[0].r == 17 && !last_frame()[1].r);
    bool on = false; assert(zmk_rgb_matrix_get_state(&on) == 0 && on);
    assert(atomic_get(&kp_rgb_output_allowed));
    host_run_until(now + 64); /* and it stays that way while the frames go on */
    assert(last_frame()[0].r == 17 && !last_frame()[1].r);

    policy_changed(true, true); /* both dark: the strip is off, with a black frame */
    assert(zmk_rgb_matrix_get_state(&on) == 0 && !on && ctx.state.user_on);
    assert(!atomic_get(&kp_rgb_output_allowed));
    assert(!last_frame()[0].r && !last_frame()[1].r);
    host_run_until(now + 64);
    assert(!last_frame()[0].r && !last_frame()[1].r);

    policy_changed(false, true); /* the keys are back; the underglow is dark */
    assert(zmk_rgb_matrix_get_state(&on) == 0 && on);
    assert(!last_frame()[0].r && last_frame()[1].r == 17);

    policy_changed(false, false); /* everything is back, as it was */
    assert(last_frame()[0].r == 17 && last_frame()[1].r == 17);

    /* the user's switch wins over the policy */
    assert(zmk_rgb_matrix_off() == 0); host_run_ready();
    assert(zmk_rgb_matrix_get_state(&on) == 0 && !on);
    policy_changed(false, false);
    assert(zmk_rgb_matrix_get_state(&on) == 0 && !on);
    assert(zmk_rgb_matrix_on() == 0); host_run_ready();
    assert(zmk_rgb_matrix_get_state(&on) == 0 && on);

    /* ZMK going idle changes nothing: the strip stays on, and the effects are not
     * told to dim to the idle brightness */
    host_activity(false); host_run_ready();
    host_run_until(now + 64);
    assert(zmk_rgb_matrix_get_state(&on) == 0 && on);
    assert(!host_frame_idle);
    assert(last_frame()[0].r == 17 && last_frame()[1].r == 17);
    host_activity(true); host_run_ready();
    assert(!host_frame_idle);

    puts("idle LEDs: truth table, masks, both groups, the user's switch and ZMK idle passed");
}
'''

if __name__ == '__main__':
    run_tests(FIXTURE + TESTS, scheduler=True, auto_off_idle=False, display_settings=True,
              allow_unused=True)

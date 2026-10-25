/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * A stand-in of the idle policy (src/idle_policy.c) for the host test of
 * src/display_settings.c: the test says what the policy says (g_idle_screen_off,
 * g_idle_dimmed) and counts what display_settings.c asks of it. The policy itself is
 * tested apart, in tests/unit/test_idle_policy.py.
 */
#include "stub_all.h"

#include "idle_policy.h"

bool g_idle_screen_off;
bool g_idle_dimmed;
int g_idle_activity_calls;
int g_idle_config_calls;

bool kp_idle_screen_off(void) { return g_idle_screen_off; }
bool kp_idle_dimmed(void) { return g_idle_dimmed; }
void kp_idle_note_activity(void) { g_idle_activity_calls++; }
void kp_idle_config_changed(void) { g_idle_config_calls++; }

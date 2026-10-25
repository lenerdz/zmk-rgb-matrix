#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Host-test the idle policy (src/idle_policy.c and the pure rules of idle_policy.h).

idle_policy.c decides when the screen dims and goes off and when the LEDs under the keys
and the underglow go off: each with its own mode (never, only on battery, always) and its
time, from the last key on either half, USB being plugged or unplugged and what the
central tells the peripheral. The file is compiled as it is, against the stand-ins of
tests/unit/display_settings (a fake clock, work queues and USB state) and the few
functions it calls in the rest of the module, recorded by tests/unit/idle_policy/test_main.c.
Each scenario runs in its own process, because the file keeps its state in statics. It
checks:

  - the pure rules: which modes are armed with and without USB, the deadline of each
    thing, the nearest one, the dim that counts 1 to 9 seconds as 10 and 0 as never, the
    contrast of a dimmed screen (never black, never past the range);
  - the walk of the anti burn-in shift: it starts in the middle, moves one pixel at a
    time, visits every position of the square and repeats;
  - the policy over time: nothing goes off with every mode on never; the defaults, with
    and without USB; USB plugged and unplugged; the mode always; the order of the things;
    a key that postpones the deadline; the wake by a key of either half, and which key is
    reported to the peripheral; what the central says about USB and for how long it is
    believed; its first report; keys the central reports; a change of the settings in either
    direction; a key that comes in the middle of an evaluation; a keyboard with no saved
    settings.

It does not run on a keyboard: the clock, the work queues and the USB state are the
stand-ins.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
HARNESS = Path(__file__).resolve().parent / "idle_policy"
STUBS = Path(__file__).resolve().parent / "display_settings"

# Headers that idle_policy.c (and the ones it includes) ask for; each one is the
# stand-in header of the display settings test.
SHIMS = [
    "zephyr/device.h",
    "zephyr/init.h",
    "zephyr/kernel.h",
    "zephyr/logging/log.h",
    "zephyr/sys/util.h",
    "zmk/event_manager.h",
    "zmk/events/position_state_changed.h",
    "zmk/events/usb_conn_state_changed.h",
    "zmk/usb.h",
    "dt-bindings/zmk/rgb.h",
]

DEFINES = [
    "-DCONFIG_KEYPAW_DISPLAY_SETTINGS=1",
    "-DCONFIG_USB_DEVICE_STACK=1",
    "-DCONFIG_ZMK_SPLIT=1",
    "-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1",
    "-DCONFIG_KEYPAW_RGB_SPLIT_SYNC=1",
    "-DCONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS=30000",
    "-DCONFIG_ZMK_LOG_LEVEL=0",
]

SCENARIOS = [
    "pure",
    "shift_walk",
    "never",
    "defaults_on_battery",
    "defaults_on_usb",
    "usb_plug",
    "always",
    "order",
    "activity_postpones",
    "wake_and_kick",
    "peer_usb",
    "peer_first_report",
    "peer_key",
    "config",
    "key_during_evaluation",
    "no_saved_settings",
]


def write_shims(directory):
    stand_in = (STUBS / "stub_all.h").as_posix()
    for shim in SHIMS:
        path = directory / shim
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "%s"\n' % stand_in)


def build(directory):
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(os.environ.get("CFLAGS", ""))
    executable = directory / "test_idle_policy"
    subprocess.run(
        compiler
        + [
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            *flags,
            "-I", str(directory / "stubs"),
            "-I", str(STUBS),
            "-I", str(INCLUDE),
            "-I", str(SRC),
            *DEFINES,
            str(SRC / "idle_policy.c"),
            str(STUBS / "stub_impl.c"),
            str(HARNESS / "test_main.c"),
            "-o", str(executable),
        ],
        check=True,
    )
    return executable


def run(executable):
    failed = []
    for scenario in SCENARIOS:
        result = subprocess.run(
            [str(executable), scenario], capture_output=True, text=True
        )
        print(result.stdout, end="")
        if result.returncode != 0:
            print(result.stderr, end="")
            failed.append(scenario)
    return failed


def main():
    with tempfile.TemporaryDirectory(prefix="idle-policy-") as directory:
        directory = Path(directory)
        write_shims(directory / "stubs")
        failed = run(build(directory))
    if failed:
        raise SystemExit("idle policy tests failed: " + ", ".join(failed))
    print("idle policy tests passed")


if __name__ == "__main__":
    main()

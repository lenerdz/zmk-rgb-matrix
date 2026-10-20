#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Host-test the logic of the display (OLED) settings (src/display_settings.c).

The file is compiled as it is, against small stand-ins for the Zephyr and ZMK APIs it
uses (tests/unit/display_settings/stub_all.h and stub_impl.c: a fake clock, work queues,
display, settings store, activity and USB state). Each scenario of test_main.c runs in
its own process, because the file keeps its state in statics. It checks:

  - the ranges, the defaults and the answers for an id or a value that does not exist;
  - the flash records: one per setting that differs from its default, written once after
    the debounce, deleted when the setting is back to its default, loaded with the bad
    ones ignored, deleted at once by the settings reset;
  - the contrast of each half (the central applies the left one, the peripheral the right
    one), at boot and at every change;
  - when the screen goes off: the modes never, only on battery and always, the idle time of
    ZMK, USB being plugged and unplugged, the wake of a change made from ZMK Studio;
  - the wire format of the command that carries a setting to the peripheral.

It does not run on a keyboard: the display driver, the work queues and the settings
backend are the stand-ins.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
HARNESS = Path(__file__).resolve().parent / "display_settings"

# Headers that display_settings.c (and the ones it includes) ask for; each one is the
# stand-in header.
SHIMS = [
    "zephyr/device.h",
    "zephyr/devicetree.h",
    "zephyr/drivers/display.h",
    "zephyr/init.h",
    "zephyr/kernel.h",
    "zephyr/logging/log.h",
    "zephyr/settings/settings.h",
    "zephyr/sys/atomic.h",
    "zephyr/sys/util.h",
    "zmk/activity.h",
    "zmk/display.h",
    "zmk/event_manager.h",
    "zmk/events/activity_state_changed.h",
    "zmk/events/usb_conn_state_changed.h",
    "zmk/usb.h",
    "dt-bindings/zmk/rgb.h",
]

COMMON_DEFINES = [
    "-DCONFIG_KEYPAW_DISPLAY_SETTINGS=1",
    "-DCONFIG_ZMK_IDLE_TIMEOUT=30000",
    "-DCONFIG_USB_DEVICE_STACK=1",
    "-DCONFIG_KEYPAW_DISPLAY_TOAST_MS_DEFAULT=1500",
    "-DCONFIG_KEYPAW_DISPLAY_SAVE_DEBOUNCE_MS=3000",
    "-DCONFIG_SSD1306_DEFAULT_CONTRAST=128",
    "-DCONFIG_ZMK_LOG_LEVEL=0",
]

# The central half of a split keyboard: it drives the left screen and pushes the settings
# to the peripheral. The peripheral drives the right screen and has no sync.
CENTRAL_DEFINES = [
    "-DCONFIG_ZMK_SPLIT=1",
    "-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1",
    "-DCONFIG_KEYPAW_RGB_SPLIT_SYNC=1",
]
PERIPHERAL_DEFINES = ["-DCONFIG_ZMK_SPLIT=1"]

CENTRAL_SCENARIOS = [
    "defaults",
    "contrast_persist",
    "debounce_batch",
    "range",
    "never",
    "always",
    "always_min",
    "battery",
    "wake_while_blanked",
    "back_to_never",
    "mode_change_while_idle",
    "load",
    "boot_apply",
    "restore",
    "reset",
    "sync",
    "command",
    "central_applies_left",
]
PERIPHERAL_SCENARIOS = ["peripheral", "peripheral_boot", "sync"]


def write_shims(directory):
    stand_in = (HARNESS / "stub_all.h").as_posix()
    for shim in SHIMS:
        path = directory / shim
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "%s"\n' % stand_in)


def build(name, defines, directory):
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(os.environ.get("CFLAGS", ""))
    executable = directory / name
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
            "-I", str(HARNESS),
            "-I", str(INCLUDE),
            "-I", str(SRC),
            *COMMON_DEFINES,
            *defines,
            str(SRC / "display_settings.c"),
            str(HARNESS / "stub_impl.c"),
            str(HARNESS / "test_main.c"),
            "-o", str(executable),
        ],
        check=True,
    )
    return executable


def run(executable, scenarios):
    failed = []
    for scenario in scenarios:
        result = subprocess.run(
            [str(executable), scenario], capture_output=True, text=True
        )
        print(result.stdout, end="")
        if result.returncode != 0:
            print(result.stderr, end="")
            failed.append(scenario)
    return failed


def main():
    with tempfile.TemporaryDirectory(prefix="display-settings-") as directory:
        directory = Path(directory)
        write_shims(directory / "stubs")
        central = build("test_central", CENTRAL_DEFINES, directory)
        peripheral = build("test_peripheral", PERIPHERAL_DEFINES, directory)
        failed = run(central, CENTRAL_SCENARIOS)
        failed += run(peripheral, PERIPHERAL_SCENARIOS)
    if failed:
        raise SystemExit("display settings tests failed: " + ", ".join(failed))
    print("display settings tests passed")


if __name__ == "__main__":
    main()

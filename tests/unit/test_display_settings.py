#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Host-test the logic of the display (OLED) settings (src/display_settings.c).

The file is compiled as it is, against small stand-ins for the Zephyr and ZMK APIs it
uses (tests/unit/display_settings/stub_all.h and stub_impl.c: a fake clock, work queues,
display, settings store and USB state) and against a stand-in of the idle policy
(stub_idle.c: the test says what the policy says, and sees what is asked of it). Each
scenario of test_main.c runs in its own process, because the file keeps its state in
statics. It checks:

  - the ranges, the defaults and the answers for an id or a value that does not exist,
    among them the nine settings of the lights and the anti burn-in (ids 7 to 15);
  - the flash records: one per setting that differs from its default, written once after
    the debounce, deleted when the setting is back to its default, loaded with the bad
    ones ignored, deleted at once by the settings reset;
  - the contrast of each half (the central applies the left one, the peripheral the right
    one), at boot and at every change;
  - that the screen does what the idle policy says (off, on, dimmed to a percent of the
    contrast of this half, never black), the contrast going before the light when the
    screen wakes, and that a change of a setting counts as activity and makes the policy
    read the settings again (the policy itself is tested apart: test_idle_policy.py);
  - the wire format of the command that carries a setting to the peripheral, and which
    settings the central sends: the ones the peripheral applies, once after a connection and
    then only what changes;
  - the OLED layout (CONFIG_KEYPAW_OLED_LAYOUT, ids 16 and up), built with the options of
    zmk-nice-oled that this keyboard has: the defaults that come from them, the ranges, the
    event that tells the widgets to draw again (one for changes that follow each other, none
    for the other settings, one after the load at boot), the flash records of the entries
    past the first 32, and the table of a peripheral, which has only the right screen.

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
    "zmk/display.h",
    "zmk/event_manager.h",
    "zmk/events/display_settings_changed.h",
    "dt-bindings/zmk/rgb.h",
]

# The defaults of Kconfig, as the module declares them (the values below are the ones
# the README documents).
COMMON_DEFINES = [
    "-DCONFIG_KEYPAW_DISPLAY_SETTINGS=1",
    "-DCONFIG_KEYPAW_DISPLAY_TOAST_MS_DEFAULT=1500",
    "-DCONFIG_KEYPAW_DISPLAY_SAVE_DEBOUNCE_MS=3000",
    "-DCONFIG_KEYPAW_DISPLAY_OFF_MODE_DEFAULT=0",
    "-DCONFIG_KEYPAW_DISPLAY_OFF_AFTER_S_DEFAULT=60",
    "-DCONFIG_KEYPAW_DISPLAY_KEYS_OFF_MODE_DEFAULT=1",
    "-DCONFIG_KEYPAW_DISPLAY_KEYS_OFF_AFTER_S_DEFAULT=300",
    "-DCONFIG_KEYPAW_DISPLAY_GLOW_OFF_MODE_DEFAULT=1",
    "-DCONFIG_KEYPAW_DISPLAY_GLOW_OFF_AFTER_S_DEFAULT=300",
    "-DCONFIG_KEYPAW_DISPLAY_SHIFT_DEFAULT=1",
    "-DCONFIG_KEYPAW_DISPLAY_SHIFT_EVERY_S_DEFAULT=60",
    "-DCONFIG_KEYPAW_DISPLAY_SHIFT_PX_DEFAULT=2",
    "-DCONFIG_KEYPAW_DISPLAY_DIM_AFTER_S_DEFAULT=120",
    "-DCONFIG_KEYPAW_DISPLAY_DIM_LEVEL_DEFAULT=25",
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

# The OLED layout, with the defaults that zmk-nice-oled gives the 128x32 screens (its
# Kconfig.defconfig) and the cat as the animation of the right screen.
LAYOUT_DEFINES = [
    "-DCONFIG_KEYPAW_OLED_LAYOUT=1",
    "-DCONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL=1",
    "-DCONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CAT=1",
    "-DCONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CUSTOM_X=18",
    "-DCONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_CUSTOM_Y=-18",
    "-DCONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y=50",
    "-DCONFIG_NICE_OLED_WIDGET_OUTPUT_BT_CUSTOM_X=4",
    "-DCONFIG_NICE_OLED_WIDGET_OUTPUT_BT_CUSTOM_Y=32",
    "-DCONFIG_NICE_OLED_WIDGET_OUTPUT_USB_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_OUTPUT_USB_CUSTOM_Y=34",
    "-DCONFIG_NICE_OLED_WIDGET_LED_TOAST_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_LED_TOAST_CUSTOM_Y=143",
    "-DCONFIG_NICE_OLED_WIDGET_PROFILE_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_PROFILE_CUSTOM_Y=137",
    "-DCONFIG_NICE_OLED_WIDGET_PROFILE_TEXT_CUSTOM_X=25",
    "-DCONFIG_NICE_OLED_WIDGET_PROFILE_TEXT_CUSTOM_Y=32",
    "-DCONFIG_NICE_OLED_WIDGET_LAYER_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_LAYER_CUSTOM_Y=146",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_GAUGE_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_GAUGE_CUSTOM_Y=70",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_X=0",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_LABEL_CUSTOM_Y=75",
]
# The widgets that only the build of the central half has (Kconfig.defconfig defines them
# under "!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL"): the peripheral build does not get them.
LEFT_WIDGET_DEFINES = [
    "-DTEST_LEFT_WIDGETS=1",
    "-DCONFIG_NICE_OLED_WIDGET_LAYER=1",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_SPEEDOMETER=1",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_NUMBER=1",
    "-DCONFIG_NICE_OLED_WIDGET_WPM_GRAPH=1",
]

CENTRAL_SCENARIOS = [
    "defaults",
    "contrast_persist",
    "debounce_batch",
    "range",
    "screen_follows_idle",
    "wake_order",
    "dim",
    "activity_on_change",
    "new_settings",
    "load",
    "boot_apply",
    "restore",
    "reset",
    "sync",
    "command",
    "central_applies_left",
]
PERIPHERAL_SCENARIOS = ["peripheral", "peripheral_boot", "sync"]

# With the OLED layout the table is longer, so the scenarios that count it (defaults, sync)
# are replaced by the layout_* ones; the others run again, to show that the entries added
# change nothing for the first six.
COMMON_LAYOUT_SCENARIOS = [
    "contrast_persist",
    "debounce_batch",
    "range",
    "screen_follows_idle",
    "dim",
    "activity_on_change",
    "new_settings",
    "load",
    "boot_apply",
    "restore",
    "reset",
    "command",
]
CENTRAL_LAYOUT_SCENARIOS = COMMON_LAYOUT_SCENARIOS + [
    "central_applies_left",
    "layout_defaults",
    "layout_range",
    "layout_picture",
    "layout_retired",
    "layout_retired_load",
    "layout_event",
    "layout_persist",
    "layout_load",
    "layout_sync",
    "layout_command",
]
PERIPHERAL_LAYOUT_SCENARIOS = [
    "peripheral",
    "peripheral_boot",
    "layout_defaults",
    "layout_picture",
    "layout_retired",
    "layout_retired_load",
    "layout_peripheral",
    "layout_peripheral_boot",
]


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
            str(HARNESS / "stub_idle.c"),
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
        central_layout = build(
            "test_central_layout",
            CENTRAL_DEFINES + LAYOUT_DEFINES + LEFT_WIDGET_DEFINES,
            directory,
        )
        peripheral_layout = build(
            "test_peripheral_layout", PERIPHERAL_DEFINES + LAYOUT_DEFINES, directory
        )
        failed = run(central, CENTRAL_SCENARIOS)
        failed += run(peripheral, PERIPHERAL_SCENARIOS)
        failed += run(central_layout, CENTRAL_LAYOUT_SCENARIOS)
        failed += run(peripheral_layout, PERIPHERAL_LAYOUT_SCENARIOS)
    if failed:
        raise SystemExit("display settings tests failed: " + ", ".join(failed))
    print("display settings tests passed")


if __name__ == "__main__":
    main()

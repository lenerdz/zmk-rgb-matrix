#!/usr/bin/env python3
# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT
"""Host-test the on-flash record of an editable colour map (src/rgb_map_codec.c)."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

SRC = Path(__file__).resolve().parents[2] / "src"

TESTS = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "rgb_map_codec.h"

#define SLOTS 54

_Static_assert(KP_RGB_MAP_RECORD_MAX_LEN <= 256,
               "a record must fit SETTINGS_MAX_VAL_LEN");

static void defaults_of(uint32_t *d) {
    for (size_t i = 0; i < SLOTS; i++) d[i] = 0x010101u * (uint32_t)(i + 1);
}

int main(void) {
    uint32_t defaults[SLOTS], colors[SLOTS], out[SLOTS];
    uint8_t rec[KP_RGB_MAP_RECORD_MAX_LEN];
    defaults_of(defaults);

    /* Nothing edited: the record is just its header. */
    memcpy(colors, defaults, sizeof(colors));
    size_t len = kp_rgb_map_record_encode(colors, defaults, SLOTS, rec);
    assert(len == KP_RGB_MAP_RECORD_HEADER_LEN);
    assert(rec[0] == KP_RGB_MAP_RECORD_VERSION && rec[1] == 0);

    /* Two edited slots (one is the last, 53) and a bit set above bit 23 that
     * must be ignored. */
    colors[3] = 0x112233;
    colors[53] = 0xFF8000 | 0xAB000000u;
    len = kp_rgb_map_record_encode(colors, defaults, SLOTS, rec);
    assert(len == KP_RGB_MAP_RECORD_HEADER_LEN + 2 * KP_RGB_MAP_RECORD_ENTRY_LEN);
    assert(rec[1] == 2);
    assert(rec[2] == 3 && rec[3] == 0x11 && rec[4] == 0x22 && rec[5] == 0x33);
    assert(rec[6] == 53 && rec[7] == 0xFF && rec[8] == 0x80 && rec[9] == 0x00);

    /* Round trip over the defaults. */
    memcpy(out, defaults, sizeof(out));
    assert(kp_rgb_map_record_decode(rec, len, SLOTS, out) == 0);
    assert(out[3] == 0x112233 && out[53] == 0xFF8000);
    for (size_t i = 0; i < SLOTS; i++) {
        if (i != 3 && i != 53) assert(out[i] == defaults[i]);
    }

    /* Setting a slot back to its default drops it from the record. */
    colors[3] = defaults[3];
    len = kp_rgb_map_record_encode(colors, defaults, SLOTS, rec);
    assert(len == KP_RGB_MAP_RECORD_HEADER_LEN + KP_RGB_MAP_RECORD_ENTRY_LEN);
    assert(rec[1] == 1 && rec[2] == 53);

    /* A default that differs from a colour only above bit 23 is no edit. */
    memcpy(colors, defaults, sizeof(colors));
    colors[10] = defaults[10] | 0x7F000000u;
    assert(kp_rgb_map_record_encode(colors, defaults, SLOTS, rec) ==
           KP_RGB_MAP_RECORD_HEADER_LEN);

    /* The worst case: every slot edited fits the record buffer. */
    for (size_t i = 0; i < SLOTS; i++) colors[i] = defaults[i] ^ 0x00FFFFFFu;
    len = kp_rgb_map_record_encode(colors, defaults, SLOTS, rec);
    assert(len == KP_RGB_MAP_RECORD_HEADER_LEN + SLOTS * KP_RGB_MAP_RECORD_ENTRY_LEN);
    assert(len <= KP_RGB_MAP_RECORD_MAX_LEN && len <= 256);
    memcpy(out, defaults, sizeof(out));
    assert(kp_rgb_map_record_decode(rec, len, SLOTS, out) == 0);
    assert(memcmp(out, colors, sizeof(out)) == 0);

    /* Damaged records are refused and leave the colours untouched. */
    uint8_t bad[16];
    uint32_t keep[SLOTS];
    memcpy(keep, defaults, sizeof(keep));

    assert(kp_rgb_map_record_decode(rec, 1, SLOTS, keep) == -EINVAL);   /* too short */
    memcpy(bad, "\x01\x01\x03\x11\x22\x33", 6);
    bad[0] = 2;                                                         /* version */
    assert(kp_rgb_map_record_decode(bad, 6, SLOTS, keep) == -EINVAL);
    bad[0] = KP_RGB_MAP_RECORD_VERSION;
    assert(kp_rgb_map_record_decode(bad, 5, SLOTS, keep) == -EINVAL);   /* truncated */
    assert(kp_rgb_map_record_decode(bad, 7, SLOTS, keep) == -EINVAL);   /* trailing byte */
    bad[2] = SLOTS;                                                     /* slot out of range */
    assert(kp_rgb_map_record_decode(bad, 6, SLOTS, keep) == -EINVAL);
    memcpy(bad, "\x01\x02\x03\x11\x22\x33\x03\x44\x55\x66", 10);        /* duplicate slot */
    assert(kp_rgb_map_record_decode(bad, 10, SLOTS, keep) == -EINVAL);
    memcpy(bad, "\x01\x02\x03\x11\x22\x33\x36\x44\x55\x66", 10);        /* 2nd entry out of range */
    assert(kp_rgb_map_record_decode(bad, 10, SLOTS, keep) == -EINVAL);
    assert(memcmp(keep, defaults, sizeof(keep)) == 0);                  /* nothing was applied */
    assert(kp_rgb_map_record_decode(NULL, 6, SLOTS, keep) == -EINVAL);
    assert(kp_rgb_map_record_decode(bad, 10, KP_RGB_SLOT_MAX + 1, keep) == -EINVAL);

    /* A smaller slot space refuses slots that no longer exist. */
    memcpy(bad, "\x01\x01\x2A\x11\x22\x33", 6);                         /* slot 42 */
    assert(kp_rgb_map_record_decode(bad, 6, 42, keep) == -EINVAL);
    assert(kp_rgb_map_record_decode(bad, 6, 43, keep) == 0 && keep[42] == 0x112233);

    puts("map record codec tests passed");
    return 0;
}
'''


def main():
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(os.environ.get("CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="rgb-map-codec-") as directory:
        directory = Path(directory)
        test = directory / "test_map_codec.c"
        executable = directory / "test_map_codec"
        test.write_text(TESTS)
        subprocess.run(compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
            "-I", str(SRC), str(SRC / "rgb_map_codec.c"), str(test),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

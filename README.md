# ZMK RGB Matrix

[English](./README.md) | [Português (Brasil)](./README.pt-BR.md)

[![GitHub Actions Workflow
Status](https://img.shields.io/github/actions/workflow/status/gudzpoz/zmk-rgb-matrix/previews.yml?label=previews)](https://gudzpoz.github.io/zmk-rgb-matrix/)
[![GitHub Actions Workflow
Status](https://img.shields.io/github/actions/workflow/status/gudzpoz/zmk-rgb-matrix/tests.yml?label=tests)](https://github.com/gudzpoz/zmk-rgb-matrix/actions/workflows/tests.yml)

<table><tbody>
<tr>
<th><center>reactive-gradient</center></th>
<th><center>rainbow-pinwheel</center></th>
<th><center>starlight-smooth</center></th></tr>
<tr>
<td><img alt="reactive-gradient" src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-gradient.gif"></td>
<td><img alt="rainbow-pinwheel" src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-pinwheel.gif"></td>
<td><img alt="starlight-smooth" src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-smooth.gif"></td>
</tr>
</tbody></table>

An extensible, position-aware / per-key LED RGB matrix framework for ZMK, with
split support and indicator overlays. This module is developed against ZMK
`main` and Zephyr 4.1. See [Built-in effects](#built-in-effects) for more
previews of everything it ships with.

## Installation

Add the repository to the `config/west.yml` in a ZMK config repository:

```yaml
manifest:
  remotes:
    - name: gudzpoz
      url-base: https://github.com/gudzpoz
  projects:
    - name: zmk-rgb-matrix
      remote: gudzpoz
      revision: main
```

## Usage

For the RGB matrix to work, you will need to provide (1) the positions of the
LEDs and (2) configuratinos for the actual lighting effects. Specifying LED
positions can be particularly tricky, but nevertheless, necessary, so bear with
me as we walk through it.

### LED position definitions

First, please follow the official ZMK guide to define for your keyboard:

- [a `zmk,physical-layout`, describing key positions with its `keys`
  property](https://zmk.dev/docs/hardware-integration/physical-layouts#optional-keys-property)

- and [your LED strip
  hardware](https://zmk.dev/docs/hardware-integration/lighting/underglow), but
  make sure to leave out the `zmk,underglow = &led_strip;` step: we'll be using
  our position-aware `keypaw,rgb-matrix` instead of the built-in
  `zmk,underglow`.

Now, as you probably know, your addressable LEDs are chained together, and the
order that they're chained are not always the same as that of your physical
keys. And that is why, in addition to specifying your physical layout and LED
strip, you now need to define that exact order for the RGB matrix with the
`mapping` attribute:

```dts
#include <dt-bindings/keypaw/rgb_matrix.h>

/ {
    rgb_matrix: rgb-matrix {
        compatible = "keypaw,rgb-matrix";
        strip = <&led_strip>;
        physical-layout = <&default_layout>;
        mapping = <0 1 2 KP_RGB_NO_KEY(350, 50)>;
    };
};
```

> The `&led_strip` and `&default_layout` in the config should match what you've
> previously defined. For example, if you are following the official guide like this:
>
> ```dts
> / {
>     physical_layout0: physical_layout_0 {
>         compatible = "zmk,physical-layout";
>         display-name = "Default Layout";
>     };
> };
> ```
>
> Then you should be using `&physical_layout0` instead of `&default_layout`.

Each entry in this `mapping = <0 1 2 KP_RGB_NO_KEY(350, 50)>;` thing specifies
the position of an LED: an integer `N` means a per-key LED under the `N`-th key
(`0` means that LED is under the key represented by the first
`&key_physical_attrs` entry in your physical layout, while `5` means the sixth
entry). The mapping length must match the strip's `chain-length`.

> Use `KP_RGB_NO_KEY(x, y)` for an LED that is not directly underneath a key;
> coordinates use the layout's units (`100` is one key width, similar to
> `&key_physical_attrs`).

> A split central with no local LEDs (a USB dongle) should omit `strip` and then
> explicitly set `mapping = <>;`. It renders nothing, but still drives the
> peripherals' effects, overlays and split sync.

Confused? Let's break it down. First, I recommend using [ZMK physical layout
converter](https://zmk-physical-layout-converter.streamlit.app/) to visualize
your actual key ordering: open the page, paste in your ZMK layout in the "ZMK
DTS" panel and click "Update JSON using this", after which you should see a
visualization with your keys numbered:

![Example of a key order visualization](./docs/key-order.svg)

Now, bring out your PCB design and trace through your LED chain. Then (maybe
mentally) overlay the chain on your keyboard layout, and that is what you need
for your RGB matrix `mapping`. For example, the following PCB and keyboard
layout give <code>mapping = &lt;6 7 8 9 10 11 &nbsp;&nbsp; 23 22 21 20 19 18
&nbsp;&nbsp; 30 31 32 33 34 35 ...&gt;;</code>.

<table><tbody>
<tr><td><img alt="The order of the LED chain on a PCB" src="./docs/led-order.svg"></td>
<td>→</td>
<td><img alt="The order of the LED chain overlaid on the key layout" src="./docs/key-in-led-order.svg"></td></tr>
</tbody></table>

> The example above also has a few LEDs with no corresponding keys, for which you will need to calculate the coordinates yourself.
> Here is the full mapping for the example:
> ```
> / {
>     rgb_matrix: rgb-matrix {
>         compatible = "keypaw,rgb-matrix";
>         strip = <&led_strip>;
>         physical-layout = <&default_layout>;
>
>         mapping = <
>         6 7 8 9 10 11
>         23 22 21 20 19 18
>         30 31 32 33 34 35
>
>         KP_RGB_NO_KEY(1450, 387) // between 35 & 47
>         KP_RGB_NO_KEY(1350, 387) // between 34 & 46
>         KP_RGB_NO_KEY(1250, 400) // ~to the left of the previous LED
>         KP_RGB_NO_KEY(1180, 350) // ~to the top left of the previous LED
>         47 46
>         KP_RGB_NO_KEY(1000, 500) // ~to the right of 42
>         42
>         KP_RGB_NO_KEY(850, 475)  // ~to the left of 42
>         43 44 45
>         >;
>     };
> };
> ```
>
> A split keyboard normally declares one matrix node and one local mapping on
> each half. See my ZMK config for a rather complex example: [`rgb_matrix` for
> the left
> half](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48_left.overlay),
> [`rgb_matrix` for the right
> half](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48_right.overlay)
> and [the `physical_layout` they map
> to](https://github.com/gudzpoz/keypaw48-zmk/blob/main/boards/shields/keypaw48/keypaw48-layouts.dtsi).

> If your board contains multiple LED strips, you can use the
> `zmk,led-strip-composite` driver implemented by this module to concatenate
> them into one single strip, which can then be used in the RGB matrix
> definition.

### Effect configurations

With the LEDs specified, you can now configure your RGB effects. Basically, you
need a `keypaw,behavior-rgb-matrix` devicetree node, with the effects you want
as its children:

```dts
/ {
    behaviors {
        kprgb: kprgb {
            compatible = "keypaw,behavior-rgb-matrix";
            #binding-cells = <2>;
            initial-effect = <0>;

            fx_breathe: fx_breathe {
                compatible = "keypaw,rgb-matrix-breathe";
                #binding-cells = <0>;
                color = <0x0000FF>;
                duration = <2400>;
            };
            fx_rainbow: fx_rainbow {
                compatible = "keypaw,rgb-matrix-rainbow";
                #binding-cells = <0>;
                color = <0xFFFFFF>;
                duration = <4000>;
                basis = "x";
                direction = "out";
                palette = "rainbow";
            };
            fx_heatmap: fx_heatmap {
                compatible = "keypaw,rgb-matrix-heatmap";
                #binding-cells = <0>;
                color = <0xFFFFFF>;
                decrease-delay-ms = <25>;
                spread = <150>;
                area-limit = <16>;
                increase-step = <32>;
            };
        };
    };
};
```

> For split builds, you need to make sure that boards get the same effect
> definitions: an effect is identified by its position in the registry, so a
> different set or order desyncs the halves. The same applies to overlays: both
> the split state bit and the paint order come from an overlay's position under
> `rgb_overlays`, so both halves must declare the same overlays in the same
> order.

### Optional overlay configurations

Optionally, you may also specify some LED overlays to indicate CapsLock or the
current active layer, for example. The built-in overlay composites: it renders
one effect while a condition is active, and blends the result over the LEDs it
targets.

```dts
/ {
    rgb_conditions: rgb_conditions {
        compatible = "keypaw,rgb-conditions";
        // Host CapsLock state
        cond_caps: cond_caps {
            compatible = "keypaw,rgb-condition-caps-lock";
        };
        // Layer 1 active state
        cond_layer1: cond_layer1 {
            compatible = "keypaw,rgb-condition-layer";
            layer = <1>;
        };
        // When the user toggles &tg_batt with key bindings
        cond_batt: cond_batt {
            compatible = "keypaw,rgb-condition-latch";
            tg_batt: tg_batt {
                compatible = "keypaw,behavior-rgb-overlay-toggle";
                #binding-cells = <0>;
            };
        };
    };

    rgb_overlays: rgb_overlays {
        compatible = "keypaw,rgb-overlays";
        // Light up Key No. 41 when CapsLock is active
        caps: caps {
            compatible = "keypaw,rgb-overlay";
            condition = <&cond_caps>;
            keys = <41>;
            fx_caps {
                compatible = "keypaw,rgb-matrix-solid";
                #binding-cells = <0>;
                color = <0x00FFFF>;
            };
        };
        // Light up keys when Layer 1 is active
        layer_fn1: layer_fn1 {
            compatible = "keypaw,rgb-overlay";
            condition = <&cond_layer1>;
            keys = <13 14 15 16 17 18 19 20 21 22>;
            fx_fn1 {
                compatible = "keypaw,rgb-matrix-solid";
                #binding-cells = <0>;
                color = <0xFF8000>;
            };
        };
        // A built-in battery indicator overlay
        batt: batt {
            compatible = "keypaw,rgb-overlay-battery";
            condition = <&cond_batt>;
            all-leds;
            color = <0x00FF00>;
        };
    };
};
```

A generic overlay must contain exactly one enabled effect child (as above).
Unlike "public" effects listed under the `&kprgb` node, these "private" child
effects are owned by the overlay and can only be activated by an active overlay
condition.

There are some more attributes for the overlay: `opacity = <...>;` adjusts how
much of the output is mixed into the layers below; `all-leds;` is a convenient
way to express that the effect renders over all LEDs (and, alternatively, you
can limit the overlay effect to a few LEDs, specified with `keys` or `leds`).

`keypaw,rgb-conditions` and `keypaw,rgb-overlays` are plain container nodes,
serving as registries. An overlay's declaration order under `rgb_overlays` is
its paint order: a later child composites on top. Every registered overlay is
composited by default; a controller-selectable effect may narrow that list with its
own `overlays`/`no-overlays`.

> An explicit `overlays = <&a &b>;` list also sets its own paint order;
> duplicate placements in that list are rejected.

> The `cond_batt` latch condition is a bit different from other conditions
> above: it allows the user to toggle the condition via key bindings. Just bind
> `&tg_batt` anywhere in the keymap.

### Controlling the RGB matrix

The `keypaw,behavior-rgb-matrix` (`&kprgb`) behavior defined above can be used
to control the RGB engine, allowing you to adjust the current color, switch
between effects and turn the LEDs on and off. See [RGB Underglow
Behavior](https://zmk.dev/docs/keymaps/behaviors/underglow) in ZMK documentation
for details: just replace their `&rgb_ug` with your `&kprgb`.

```dts
bindings = <
    &kprgb RGB_TOG &fx_solid &fx_reactive
    &kprgb RGB_BRI &kprgb RGB_BRD
    &kprgb RGB_RESET
>;
```

Effect state is persisted when `CONFIG_SETTINGS=y`; otherwise the configured
initial state is used at boot. The matrix turns off on idle by default;
`CONFIG_KEYPAW_RGB_MATRIX_AUTO_OFF_IDLE=n` keeps it lit and applies
`idle-brightness` while idle. With `CONFIG_KEYPAW_DISPLAY_SETTINGS` on, ZMK's idle state
is not used: the LEDs go off by the limits of the display settings (see
[When the screen and the lights go off](#when-the-screen-and-the-lights-go-off)), and
those two options do nothing.

> In addition to ZMK commands, we offer a new `RGB_RESET` command. Because we
> persist per-effect states, so when you change your devicetree RGB effect
> definitions, things can go out of sync, when you might want to run this
> `&kprgb RGB_RESET` command manually.

### Change event

`CONFIG_KEYPAW_RGB_MATRIX_CHANGE_EVENT` (on by default) raises the
`zmk_rgb_matrix_changed` event (`zmk/events/rgb_matrix_changed.h`) after a `&kprgb`
or `&fx_*` command changed something the user can see. It lets other code, such
as a status screen, show what a key press did.

| `kind` | Raised when |
|---|---|
| `ZMK_RGB_MATRIX_CHANGE_EFFECT` | another effect became the active one |
| `ZMK_RGB_MATRIX_CHANGE_POWER` | the user turned the lights on or off |
| `ZMK_RGB_MATRIX_CHANGE_BRIGHTNESS` | brightness went up or down |
| `ZMK_RGB_MATRIX_CHANGE_HUE` | hue went up or down |
| `ZMK_RGB_MATRIX_CHANGE_SATURATION` | saturation went up or down |
| `ZMK_RGB_MATRIX_CHANGE_PERIOD` | the active effect's animation period changed |
| `ZMK_RGB_MATRIX_CHANGE_RESET` | `RGB_RESET` put the state back to the defaults |

The event carries the state after the change: `on` (the user's on/off intent),
`effect` (position among the children of `&kprgb`, 0 based), `effect_count`,
`effect_name` (the node name without the leading `fx_`; static storage, may be
`NULL`), `hue` (0 to 359), `saturation` and `brightness` (0 to 100) and
`period_ms` (a shorter period is a faster animation).

- A command that changed nothing (the same effect again, a brightness already at
  its limit) raises nothing. One command changes one thing, so the `kind` is the
  first row of the table that differs.
- The commands are global, so a split keyboard raises the event on **both**
  halves. That is what lets a peripheral's screen react to a key pressed on the
  other half.
- It is not raised at boot, for the state the central pushes to a peripheral when
  it connects (those commands carry `KP_RGB_SYNC_EVENT_POSITION` as their key
  position) or for the colour-map edits of ZMK Studio: only for what the user
  asked for.
- Listeners run in the thread that applied the command: keep them short and do
  not call back into the module from them.

```c
#include <zmk/event_manager.h>
#include <zmk/events/rgb_matrix_changed.h>

static int on_rgb_changed(const zmk_event_t *eh) {
  const struct zmk_rgb_matrix_changed *ev = as_zmk_rgb_matrix_changed(eh);
  if (ev != NULL && ev->kind == ZMK_RGB_MATRIX_CHANGE_EFFECT) {
    /* ev->effect + 1 of ev->effect_count, named ev->effect_name */
  }
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(my_rgb_listener, on_rgb_changed);
ZMK_SUBSCRIPTION(my_rgb_listener, zmk_rgb_matrix_changed);
```

The [zmk-nice-oled](https://github.com/lenerdz/zmk-nice-oled) fork uses it for its
`NICE_OLED_WIDGET_LED_TOAST` widget.

## Colour slots and editable colour maps

The `static` effect can keep its colours by *slot* instead of by LED. A map built
this way is the same on both halves of a split keyboard, can be edited while the
keyboard runs, can be saved to flash and is copied to the peripheral half.
[ZMK Studio](https://zmk.dev/docs/features/studio) is how the edits normally
arrive (see [ZMK Studio requests](#zmk-studio-requests)). A `static` effect
without `layer-id` stays fixed, as before.

### Colour slots

A slot is one position of a list of colours (`slot-colors`) and means the same
thing on both halves, so a single list is valid for the whole keyboard. Declare
the size of the slot space in the `keypaw,rgb-matrix` node of each half, and
where the LEDs that are not under a key sit in it:

```dts
rgb_matrix: rgb-matrix {
    compatible = "keypaw,rgb-matrix";
    strip = <&led_strip>;
    physical-layout = <&foostan_corne_6col_layout>;
    mapping = < /* as usual */ >;

    slot-count = <54>;                   /* the same on both halves, at most 60 */
    no-key-slots = <42 43 44 45 46 47>;  /* the other half lists <48 49 50 51 52 53> */
};
```

- The slot of an LED under a key is the position of that key in the physical
  layout, so `slot-count` is at least the number of keys.
- `no-key-slots` gives the slot of each LED that is not under a key (each
  `KP_RGB_NO_KEY` entry of `mapping`), in the order of those entries. The slots
  of the other half's LEDs are left out of the list: a slot that no local LED
  paints belongs to the other half.
- A half paints only the slots of its own LEDs.

On a Corne, slots 0 to 41 are the key positions, 42 to 47 the left underglow and
48 to 53 the right underglow.

### Editable colour maps

A map is a `static` effect with `slot-colors` and, to be editable, `layer-id`:

```dts
map_fn1: fx_ov_fn1 {
    compatible = "keypaw,rgb-matrix-static";
    #binding-cells = <0>;
    layer-id = <1>;
    slot-colors = <0xFF8000 0xFF8000 /* ... one colour for every slot ... */>;
};
```

`layer-id` (0 to 14, unique among the maps) is the stable id of the keymap layer
the map belongs to (`Layer.id` in the ZMK Studio protocol), not its position, so
the map keeps following its layer when the layers are reordered. The usual place
for a map is as the effect of an overlay whose condition is that layer.

Each map keeps three copies of its colours:

| Copy | What it is | When it changes |
|---|---|---|
| defaults | the `slot-colors` of the devicetree, in ROM | only with a new firmware |
| live | what the LEDs show now | at every edit, which repaints at once |
| saved | what was last written to flash, or read from it at boot | when saved |

- Without saving, an edit lasts until the keyboard is turned off.
- Saving, on the central half only, writes one settings record per changed map
  (`keypaw/rgb_matrix/map/<layer id>`) with just the slots that differ from the
  defaults. A map that is back to its defaults loses its record. At boot the
  records are read and win over `slot-colors`, slot by slot. A record is at most
  2 + 4 x 60 = 242 bytes, below the 256 of `SETTINGS_MAX_VAL_LEN`. Saving needs
  `CONFIG_SETTINGS=y` (ZMK Studio selects it); without it, a save answers "not
  supported".
- Discarding puts the live colours back to the saved ones. Restoring the defaults
  of a layer puts its live colours back to `slot-colors` (save to keep that).
- `&kprgb RGB_RESET` and ZMK Studio's "Restore Stock Settings" delete every
  record and put every map back to its defaults.

The C API for all of this is in `src/rgb_maps.h`.

### Split keyboards

Only the central half saves. It keeps the peripheral up to date by slot, with two
commands of the split sync (`CONFIG_KEYPAW_RGB_SPLIT_SYNC`): `RGB_MAP_SET_CMD`
sets one slot of one map and `RGB_MAP_RESET_CMD` puts the maps back to their
defaults. Every edit is queued and sent one command at a time, 50 ms apart. When
the peripheral connects (or restarts), the central first sends a reset and then
only the slots of the peripheral whose colour differs from the defaults. The
peripheral never writes flash: a peripheral that is turned off comes back with
the defaults of its own firmware and receives the edits again when it reconnects.
So after changing `slot-colors` in devicetree, flash the half you changed.

### ZMK Studio requests

`CONFIG_KEYPAW_RGB_MAPS_STUDIO` (on by default when ZMK Studio's RPC is on the
build, so on the central half) adds six requests and a notification to the
`keymap` subsystem of ZMK Studio. They exist only in the
[lenerdz/zmk-studio-messages](https://github.com/lenerdz/zmk-studio-messages)
fork of the protocol, whose ids start at 100 so that they cannot collide with the
upstream ones. Like the keymap requests, all of them need the keyboard to be
unlocked.

| Request | Id | Answer | What it does |
|---|---|---|---|
| `get_led_info` | 100 | `LedInfo`: `slot_count`, `layer_ids` (up to 15) and `unsaved_changes` | tells what can be edited |
| `get_led_map` | 101 | `LedMap`: `layer_id` and `colors` (the 54 live colours, `0xRRGGBB`) | reads one map |
| `set_led_colors` | 102 | `OK`, `INVALID_LAYER`, `INVALID_SLOT` or `BUSY` (any other failure) | edits up to 16 slots of one map, all or nothing |
| `save_led_changes` | 103 | the same answer as the keymap's save: ok, or `NOT_SUPPORTED`, `NO_SPACE` or `GENERIC` | writes the changed maps to flash |
| `discard_led_changes` | 104 | `true` | puts the live colours back to the saved ones |
| `restore_led_defaults` | 105 | `LedMap`, already restored | puts one map back to its defaults |

The notification `unsaved_led_changes_status_changed` (id 100) says when the live
colours start or stop differing from the saved ones. A layer without a map in
`get_led_map` or `restore_led_defaults` answers the `GENERIC` simple error, whose
value is 0, so a client has to check for it explicitly. A firmware without these
handlers answers all of them with `RPC_NOT_FOUND`, which is how a client can tell
that the feature is missing. ZMK Studio's "Restore Stock Settings" also resets
the maps.

To use it, replace the `zmk-studio-messages` project that ZMK imports from its
`app/west.yml` with the fork, in the `config/west.yml` of your config repository
(the same name and path), and add the remote:

```yaml
manifest:
  remotes:
    - name: lenerdz
      url-base: https://github.com/lenerdz
  projects:
    - name: zmk-studio-messages
      remote: lenerdz
      revision: <commit>
      path: modules/msgs/zmk-studio-messages
```

The stock protocol has none of these requests, so building against it fails.

**Limits.** `slot-count` is at most 60. The 54 colours of `LedMap` are fixed in
the protocol: with more slots, raise `colors max_count` in `keymap.options.in` of
the fork (the build checks it). Layer ids go from 0 to 14, `get_led_info` lists up
to 15 maps and `set_led_colors` carries up to 16 slots.

**Receive buffer.** The receive buffer of ZMK Studio
(`CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE`) holds 30 bytes by default and drops the
bytes that do not fit. A request that edits 16 colours is 158 bytes once framed
(up to about 210 when many of its bytes need an escape), so put
`CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=256` in the config of the central half. The
build fails when the buffer is smaller.

**Status.** The codec of the flash record has host tests
(`tests/unit/test_map_codec.py`). The rest builds in the CI of
[lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk) and has not been tried
on a real keyboard yet: the live edit, the save to flash, the discard and the copy
to the peripheral half.

## Display (OLED) settings

`CONFIG_KEYPAW_DISPLAY_SETTINGS` (off by default) keeps a list of numbered
integer settings for the screens and the lights: the contrast of each half, when the
screen, the LEDs under the keys and the underglow go off, the light toast of the
[zmk-nice-oled](https://github.com/lenerdz/zmk-nice-oled) fork, the anti burn-in of
the screens (the pixel shift and the dimming) and, with `CONFIG_KEYPAW_OLED_LAYOUT`,
which widgets the screens draw and where (see [OLED layout](#oled-layout)). Each half
applies them at once and keeps them in its own flash, the central copies them to the
peripheral, and [ZMK Studio](https://zmk.dev/docs/features/studio) edits them (see
[ZMK Studio requests for the display](#zmk-studio-requests-for-the-display)).
A widget reads one with `kp_display_setting_get()`
(`include/zmk/display_settings.h`).

Apart from when the lights go off it has nothing to do with the colours, but the
commands that reach the peripheral travel in `&kprgb` and the copy is made by the
same split sync, so it needs `CONFIG_KEYPAW_BEHAVIOR_RGB_MATRIX`, and it has to be on
in the build of both halves. It also needs `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n` (the
build fails otherwise), because the module turns the screen off itself.

| Id | Setting | Range | Default | What it does |
|---|---|---|---|---|
| 1 | Left screen contrast | 0 to 255 | 128 (`CONFIG_SSD1306_DEFAULT_CONTRAST`) | `display_set_contrast()` of the central half's screen |
| 2 | Right screen contrast | 0 to 255 | 128 | the same, on the peripheral half |
| 3 | Screen off | 0 never, 1 only on battery, 2 always | 0 (`CONFIG_KEYPAW_DISPLAY_OFF_MODE_DEFAULT`) | whether the screen goes off when nobody types; "only on battery" keeps it on while the keyboard has USB |
| 4 | Seconds without activity before the screen goes off | 10 to 3600 | 60 (`CONFIG_KEYPAW_DISPLAY_OFF_AFTER_S_DEFAULT`) | counted from the last activity; used when setting 3 is not 0 |
| 5 | Toast | 0 or 1 | 1 | the light toast of zmk-nice-oled |
| 6 | Toast duration (ms) | 500 to 5000 | `CONFIG_KEYPAW_DISPLAY_TOAST_MS_DEFAULT` (1500) | how long the toast stays on screen |
| 7 | LEDs under the keys off | 0 never, 1 only on battery, 2 always | 1 (`CONFIG_KEYPAW_DISPLAY_KEYS_OFF_MODE_DEFAULT`) | whether the LEDs under the keys go off when nobody types |
| 8 | Seconds without activity before the LEDs under the keys go off | 10 to 3600 | 300 (`CONFIG_KEYPAW_DISPLAY_KEYS_OFF_AFTER_S_DEFAULT`) | used when setting 7 is not 0 |
| 9 | Underglow off | 0 never, 1 only on battery, 2 always | 1 (`CONFIG_KEYPAW_DISPLAY_GLOW_OFF_MODE_DEFAULT`) | the same, for the LEDs that are not under a key (the ones with `KP_RGB_NO_KEY` in the mapping of the `rgb-matrix` node) |
| 10 | Seconds without activity before the underglow goes off | 10 to 3600 | 300 (`CONFIG_KEYPAW_DISPLAY_GLOW_OFF_AFTER_S_DEFAULT`) | used when setting 9 is not 0 |
| 11 | Pixel shift | 0 or 1 | 1 (`CONFIG_KEYPAW_DISPLAY_SHIFT_DEFAULT`) | anti burn-in: from time to time everything a screen shows moves by one pixel along a short path (zmk-nice-oled does the drawing; see "Anti Burn-In: Pixel Shift" in its README) |
| 12 | Seconds between two steps of the pixel shift | 10 to 3600 | 60 (`CONFIG_KEYPAW_DISPLAY_SHIFT_EVERY_S_DEFAULT`) | used when setting 11 is 1 |
| 13 | Distance of the pixel shift | 1 to 4 | 2 (`CONFIG_KEYPAW_DISPLAY_SHIFT_PX_DEFAULT`) | pixels to each side of the place where the screen is drawn |
| 14 | Seconds without activity before the screen dims | 0 to 3600 | 120 (`CONFIG_KEYPAW_DISPLAY_DIM_AFTER_S_DEFAULT`) | anti burn-in: 0 never dims, and a value from 1 to 9 counts as 10 |
| 15 | Contrast of a dimmed screen | 5 to 90 | 25 (`CONFIG_KEYPAW_DISPLAY_DIM_LEVEL_DEFAULT`) | percentage of the normal contrast (never less than 1; a screen whose contrast is 0 stays at 0) |

The settings with the ids 16 and up are the layout of the screens, described in
[OLED layout](#oled-layout). When the screen and the lights go off is explained in
[When the screen and the lights go off](#when-the-screen-and-the-lights-go-off).

**Flash.** Each half keeps one settings record, `keypaw/display/<id>` (an int32),
for every setting that differs from its default; a setting at its default has no
record. A change is written 3 seconds after the last one
(`CONFIG_KEYPAW_DISPLAY_SAVE_DEBOUNCE_MS`, so that dragging a slider writes once)
and the records win over the defaults at boot; a record that is unknown or out of
range is ignored (the one of a picture kind that was retired is deleted by the save
that follows the boot). `&kprgb RGB_RESET` does not touch them. ZMK Studio's
"Restore Stock Settings" deletes them.

**Split keyboards.** The copy to the peripheral is the last phase of the split sync,
after the colour maps: one `RGB_DISPLAY_SET_CMD` per setting (`param1` holds the
command in its low 12 bits and the setting id in bits 12 to 19, which is why every
id stays below 256, and `param2` is the value as an int32), one command at a time,
50 ms apart. The central sends a setting only when the peripheral does not hold its
value yet: it remembers what it sent for each one, so after a change only that
setting travels. When the peripheral connects (or restarts) nothing is known about
it, so every setting it applies is sent. The central never sends the settings of its
own screen (the left contrast and, with the OLED layout, the widgets of the left
screen). The peripheral applies the value and keeps it in its own flash, so its
screen is right at boot, before the central connects. Both halves hold the fifteen
settings, and each half applies what is its own: its contrast, the limits of its
screen and of its LEDs, its pixel shift and its dimming. With the OLED layout, the
central also holds the settings of both screens and the peripheral those of the
right one.

The activity travels the other way round, from the central to the peripheral, in
`RGB_ACTIVITY_CMD` (0x107, `include/dt-bindings/keypaw/rgb_matrix.h`): `param2` has
`RGB_ACTIVITY_USB` (1: the central has USB power) and `RGB_ACTIVITY_KEY` (2: a key
was pressed on the central). While keys are pressed on the central it sends at most
one report every 3 seconds (`KP_RGB_SYNC_ACTIVITY_MIN_MS`), plus one when the typing
stops; it sends one when its USB state changes and one with every tick of the clock of
the split sync (`CONFIG_KEYPAW_RGB_SPLIT_SYNC_CLOCK_MS`, 30 seconds). Like the clock,
it stays out of the state machine of the snapshot, and one that is dropped is made up
by the next.

### When the screen and the lights go off

ZMK's own idle state is not used here. It is one timeout for everything
(`CONFIG_ZMK_IDLE_TIMEOUT`, 30 seconds), each half counts only the keys it sees (the
right half does not notice that you are typing on the left one) and it knows nothing
about USB, which is why the lights of a keyboard that was only being looked at went off
by themselves. With the display settings the module keeps the time of the last activity
itself (`src/idle_policy.c`, with the rules in `src/idle_policy.h`), and four things
follow it, each with its own limit:

| What | Settings | Default |
|---|---|---|
| the screen goes off | 3 and 4 | never (60 seconds when it is turned on) |
| the LEDs under the keys go off | 7 and 8 | only on battery, after 300 seconds |
| the underglow goes off | 9 and 10 | only on battery, after 300 seconds |
| the screen dims | 14 and 15 | after 120 seconds, to 25 percent of its contrast |

**Activity** is a key pressed on either half, a change made from ZMK Studio and the USB
cable being plugged in or out. The central counts the keys of both halves (it receives
those of the peripheral) and tells the peripheral about its own key presses and about its
USB state (`RGB_ACTIVITY_CMD`, see "Split keyboards" above), so typing on one half keeps
the other one awake and the halves go off together. A change in the USB state of the
central that the peripheral hears about counts as activity too (the first report after
the start does not). The shortest time is 10 seconds, which the peripheral can follow
because a report of the central is never more than 3 seconds late.

**The modes.** "Never" does nothing at all, which for the screen is the same as ZMK's
own option being off. "Always" goes off when the time is up. "Only on battery" goes off
only while the keyboard has no USB: the keyboard has USB when this half has USB power or,
on the peripheral, when the central says it has (the peripheral stops believing the
central after 120 seconds without a report, and counts only its own USB then). The
dimming does not depend on USB: it is the protection of a screen that never goes off.

**The screen** goes off with `display_blanking_on()` and comes back with
`display_blanking_off()`, on the display work queue (the one LVGL flushes from). Dimming
and going off are separate: the screen dims after the seconds of setting 14 (its
contrast, setting 1 or 2, drops to the percentage of setting 15, and never to 0 if it
was not 0) and goes off after the seconds of setting 4, and any activity brings back the
contrast and the light. A change made from ZMK Studio counts as activity, so it turns the
screen on and keeps it on for at least 10 seconds, so the change can be seen.

**The LEDs** are two groups: the LEDs under the keys, and the underglow (the LEDs with
`KP_RGB_NO_KEY` in the mapping of the `rgb-matrix` node). A group that is off is drawn
black after the effects and the brightness, so what the effects remember is there when
the first key press brings the group back; the on and off of `&kprgb RGB_TOG` is not
touched. When both groups are off (or the only group the strip has), the matrix stops
animating and the strip is black. `CONFIG_KEYPAW_RGB_MATRIX_AUTO_OFF_IDLE` and
`idle-brightness` do nothing while the display settings are on.

**Status.** The rules and the glue have host tests with a fake clock, fake events and a
fake split link (`tests/unit/test_idle_policy.py`, 16 scenarios: the modes, the limits,
the order of the three, the USB of the peripheral and its expiry, the report of a key and
its rate, a setting changed while the policy evaluates, a keyboard with no saved
settings), and so does what it does to the LEDs (`tests/unit/test_idle_leds.py`: which
LEDs go black for each state of the two groups, the switch of the user, ZMK's idle state
being ignored, a strip with no LED); the contrast of a dimmed screen and the walk of the
pixel shift are checked there too. It builds in the CI of
[lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk). It has not been tried on a
real keyboard yet: the lights and the screen going off by themselves after their times,
only on battery, the halves going off together, the dimming and the pixel shift.

### OLED layout

`CONFIG_KEYPAW_OLED_LAYOUT` (off by default; it needs `CONFIG_KEYPAW_DISPLAY_SETTINGS`,
the `nice_oled` shield and its status widget, `CONFIG_NICE_OLED_WIDGET_STATUS`) adds
to the settings the layout of the screens of the
[zmk-nice-oled](https://github.com/lenerdz/zmk-nice-oled) fork: for each widget,
whether it is drawn and where, and, on the right screen, which animation or image is
shown, how long one cycle takes, how the picture is turned, which piece of it shows and
how it mixes with the widgets. They are settings like the six above (the ids 16
and up), so everything said about those holds: a change is applied at once, kept in
the flash of each half, copied to the peripheral and edited from ZMK Studio (the OLED
tab of the [lenerdz/zmk-studio](https://github.com/lenerdz/zmk-studio) app is made for
this). The option has to be on in the build of both halves, and it needs the fork of
zmk-nice-oled that reads the settings: with another zmk-nice-oled the settings are
kept and synced, but nothing changes on the screens. A widget reads a value with
`kp_display_setting_get()` and the default of the build with
`kp_display_setting_default()`.

The defaults are what the build says, so the screens look as they did without the
option until something is changed: the positions come from the
`CONFIG_NICE_OLED_WIDGET_*_CUSTOM_X` and `_Y` options, and the animation from
`CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_*`. The central lists and compares the
settings of the right screen too, so set the options of the right screen in the
configuration of both halves: the central needs them for the defaults it lists, the
peripheral for its own.

Each widget has a block of eight ids, at `16 + 8 * n`: `+0` is whether it is shown (0
or 1, default 1), `+1` its x and `+2` its y, in pixels, from -160 to 320. The
animation also has `+3`, its kind, and `+4`, its cycle time. The picture needs four
more settings, which do not fit in the three ids that are left in its block (21 to 23):
the ids 112 to 115 (see "Turn, cut and mix the picture" below). A widget that the
firmware was not built with has no entry in the list that ZMK Studio gets, and the
peripheral has no entry for the left screen.

| Ids | Screen | Widget | Settings | Listed when the build has |
|---|---|---|---|---|
| 16 to 20 | right | animation or still image | shown, x, y, kind, cycle time | no `CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY` |
| 112 to 115 | right | the same animation or still image | turn, mix, corner of the piece, size of the piece | the same as the animation |
| 24 to 26 | right | battery of the right half, as a percentage | shown, x, y | always |
| 32 to 34 | right | Bluetooth icon (connected to the left half or not) | shown, x, y | always |
| 41 and 42 | right | first line of the light toast (the second is 8 pixels below) | x, y | always; the toast itself is `CONFIG_NICE_OLED_WIDGET_LED_TOAST`, and whether it is shown is setting 5 |
| 48 to 50 | left | USB or Bluetooth icon of the active output | shown, x, y | always |
| 56 to 58 | left | battery: the icon, the percentage, or the two numbers of both halves | shown, x, y | always |
| 64 to 66 | left | the five Bluetooth profile dots | shown, x, y | always |
| 72 to 74 | left | number of the active Bluetooth profile | shown, x, y | always |
| 80 to 82 | left | name of the active layer | shown, x, y | `CONFIG_NICE_OLED_WIDGET_LAYER` |
| 88 to 90 | left | WPM gauge and its needle (x and y are the gauge) | shown, x, y | `CONFIG_NICE_OLED_WIDGET_WPM_SPEEDOMETER` |
| 96 to 98 | left | WPM number | shown, x, y | `CONFIG_NICE_OLED_WIDGET_WPM_NUMBER` |
| 104 to 106 | left | WPM graph (x and y are the grid under it) | shown, x, y | `CONFIG_NICE_OLED_WIDGET_WPM_GRAPH`, and neither the bongo cat, the luna dog nor the fixed modifier indicators |

**Animation.** The kind (id 19) is one of these, offered in this order by the app (the retired ones, marked in the table, are not offered):

| Value | Kind | Frames | Time of one cycle of its own |
|---|---|---|---|
| 0 | crystal (the `CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_GEM` of the build) | 16 | 960 ms |
| 1 | cat | 8 | 960 ms |
| 2 | head | 16 | 4800 ms |
| 3 | spaceman | 20 | 4800 ms |
| 4 | pokemon | 48 | 10000 ms |
| 5 | vim (retired, it was a still image) | none | none |
| 6 | vip_marcos (retired, it was a still image) | none | none |
| 7 | equalizer | 16 | 1200 ms |
| 8 | radar | 24 | 2400 ms |
| 9 | heartbeat | 16 | 1600 ms |
| 10 | rain (digital rain) | 32 | 3200 ms |
| 11 | cube (wire cube) | 16 | 1600 ms |
| 12 | atom | 24 | 2400 ms |
| 13 | helix | 16 | 1600 ms |
| 14 | warp | 24 | 2400 ms |
| 15 | sonar | 16 | 1600 ms |
| 16 | bounce (bouncing ball) | 24 | 1200 ms |
| 17 | flame | 16 | 1200 ms |
| 18 | mountains (still image) | 1 | none |
| 19 | skyline (still image) | 1 | none |
| 20 | circuit (still image) | 1 | none |
| 21 | alien (retired, it was a still image) | none | none |
| 22 | heart (retired, it was a still image) | none | none |
| 23 | keyboard (still image) | 1 | none |
| 24 | pulsar (retired, it was a still image) | none | none |
| 25 | rocket (retired, it was a still image) | none | none |
| 26 | waterfall | 16 | 1600 ms |
| 27 | lava | 16 | 3200 ms |
| 28 | lightning | 16 | 1600 ms |
| 29 | snow | 24 | 2400 ms |
| 30 | jellyfish | 16 | 1600 ms |
| 31 | tetris | 20 | 2000 ms |
| 32 | pong | 24 | 1500 ms |
| 33 | invaders | 8 | 1600 ms |
| 34 | pacman | 16 | 1600 ms |
| 35 | life | 30 | 3000 ms |
| 36 | plasma | 24 | 2400 ms |
| 37 | spiral | 16 | 1600 ms |
| 38 | newton | 16 | 1600 ms |
| 39 | gears | 12 | 1000 ms |
| 40 | hourglass | 20 | 2000 ms |
| 41 | windmill | 16 | 1600 ms |
| 42 | fireworks | 24 | 2400 ms |
| 43 | ufo | 16 | 1600 ms |
| 44 | eye | 24 | 2400 ms |
| 45 | keywave | 24 | 2400 ms |
| 46 | coffee (retired, it was a still image) | none | none |
| 47 | cactus (retired, it was a still image) | none | none |
| 48 | lighthouse (retired, it was a still image) | none | none |
| 49 | moon_cat (still image) | 1 | none |
| 50 | robot (retired, it was a still image) | none | none |
| 51 | planet (still image) | 1 | none |
| 52 | castle (retired, it was a still image) | none | none |
| 53 | tree (retired, it was a still image) | none | none |
| 54 | skull (retired, it was a still image) | none | none |
| 55 | sword (retired, it was a still image) | none | none |
| 56 | key (retired, it was a still image) | none | none |
| 57 | antenna (retired, it was a still image) | none | none |
| 58 | potion (retired, it was a still image) | none | none |
| 59 | sailboat (retired, it was a still image) | none | none |
| 60 | compass (retired, it was a still image) | none | none |
| 61 | crown (retired, it was a still image) | none | none |
| 62 | cassette (retired, it was a still image) | none | none |
| 63 | floppy (retired, it was a still image) | none | none |
| 64 | phases (retired, it was a still image) | none | none |
| 65 | mushroom (retired, it was a still image) | none | none |

The kinds 7 to 65 are the pictures that `scripts/pictures/make_pictures.py` of zmk-nice-oled draws in two batches (the kinds 7 to 25, and the kinds 26 to 65: 20 animations, then 20 still images), all 32 pixels wide (the width of the panel); its README ("New Pictures of the Right Screen") describes them.

24 of the 66 kinds are **retired**: the still images `vim` and `vip_marcos` of the
original shield and 22 of the new ones. Only `mountains`, `skyline`, `circuit`,
`keyboard`, `moon_cat` and `planet` are left, so the app offers 42 pictures (36
animations and 6 still images). A retired kind keeps its number and its name in
`enum kp_oled_anim_kind`, but it has no picture in the firmware:
`kp_oled_anim_kind_retired()` (`include/zmk/display_settings.h`) tells which ones they
are, the keyboard does not accept one as the value of setting 19 (`INVALID_VALUE` for
the request), and the record of one that was saved before is ignored and deleted when
the settings load, so the screen goes back to the default kind.

The number of a kind is what the keyboard saves in its flash, so a picture is only ever
added at the end of this list (and one that goes away is retired, never removed), with
the same name and number in `enum kp_oled_anim_kind` (`include/zmk/display_settings.h`)
and in the picture list of zmk-nice-oled, and `KP_OLED_ANIM_KIND_COUNT` goes up.

The default kind is the one the build chose: `CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_HEAD`,
`_CAT`, `_SPACEMAN` or `_POKEMON` (the first one that is on, in that order, and the
crystal if none is; `CONFIG_NICE_OLED_WIDGET_STATIC_IMAGE_PERIPHERAL_VIM` and
`_VIP_MARCOS` ask for a retired kind, so the crystal is the default then). The cycle time
(id 20) is 0, or from 100 to 60000 ms: 0 is the time that belongs to the animation (the
one the build chose keeps `CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_MS`), and a value
from 1 to 99 counts as 100. A still image has no cycle. With the option on, zmk-nice-oled
builds every animation in, so the choice can change while the keyboard is running.

**Coordinates of the animation.** Every widget but one is drawn on a canvas of 68 x 160
pixels, upright, which zmk-nice-oled rotates by 270 degrees before it goes to the
128 x 32 panel. The panel shows only the strip x 0 to 31, y 32 to 159 of it, so x goes
right and y goes down the way the screen is looked at on the desk. The animation (or
the image) is the exception: its frames are already rotated, and the firmware places
them on the rotated canvas, so its x and y are the corner of the picture on the
landscape canvas, which is the upright one turned by 90 degrees. For a picture of
height h (as it is looked at, after the turn of setting 112) whose upright corner is
(u, v), the settings are x = 160 - v - h and y = u. A
larger x moves the picture up, and a larger y moves it to the right. With the position
of the build, x = 18 and y = -18, an animation of 68 x 69 pixels has its upright corner
at u = -18, v = 73. The OLED tab of the app does this conversion; do it yourself only
if you write the values some other way.

**Turn, cut and mix the picture.** Four more settings, at the ids 112 to 115, say how the
picture is put on the canvas. They are listed with the animation and mean something only
on the right screen:

| Id | Setting | Range | Default | What it does |
|---|---|---|---|---|
| 112 | Turn | 0 to 3 | 0 | quarter turns clockwise, as the picture is looked at |
| 113 | Mix | 0 or 1 | 1 | how a lit pixel of the picture mixes with what the widgets drew under it: 0 (`KP_OLED_MIX_ADD`) lights the pixel whatever was there, so a widget gets lost where the picture is bright; 1 (`KP_OLED_MIX_INVERT`) flips what was there, so a widget over a bright part of the picture shows in reverse and stays readable |
| 114 | Corner of the piece | 0 to 65535 | 0 | `x \| y << 8`: the corner of the piece of the picture that shows, in pixels of the picture as it is looked at (after the turn); a corner past the picture is taken as its last pixel |
| 115 | Size of the piece | 0 to 65535 | 0 | `w \| h << 8`: a width or a height of 0 is the whole picture, and what goes past the picture is cut off |

The piece is two settings of 8 bits apiece, so that a change of the piece is one write:
it starts at most 255 pixels from the left or the top, and is at most 255 pixels wide and
tall. The position of the animation (ids 17 and 18) is the corner of the **whole** picture
after the turn, and a piece only hides the rest of it, so the picture stays where it is
when the piece changes. A turn does not change the position: with the same x and y the
bottom left corner of the picture, as it is looked at, stays where it was, and the OLED
tab of the app sends a new position with the turn, to turn the picture about its center.
Only the picture turns: the text widgets are drawn by fonts that cannot be turned.
zmk-nice-oled puts the picture on the canvas after the widgets and after the canvas was
rotated for the panel, which is why a widget can be over it (see "Picture on the Canvas:
Turn, Crop and Mix" in
[its README](https://github.com/lenerdz/zmk-nice-oled#picture-on-the-canvas-turn-crop-and-mix)).

**Drawing again.** The widgets read the values every time they draw, and the event
`zmk_display_settings_changed` (`zmk/events/display_settings_changed.h`) tells them to
draw again. Changes that follow one another within 20 ms make one event (restoring the
defaults, or the central pushing the whole layout to a peripheral), and the event says
only the last id of the burst, for the log: a listener reads the values it needs. It is
also raised once after the saved settings are loaded at boot, because a widget that drew
before that drew with the defaults. Listeners run in the thread of ZMK's event manager,
so hand the drawing to the display work queue, the way the widgets of zmk-nice-oled do.

**Not covered.** The bongo cat, the luna dog, the responsive bongo cat, the modifier and
HID indicators, the raw HID widgets, the sleep art and the smart battery animation stay
options of the build and are drawn where the build says. The e-paper screens (nice!view
and the like) are not supported.

**Cost.** Measured in the CI of [lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk),
with the option on against off: the right half grows 254,652 bytes of flash (every
animation and image is built in: 167,216 bytes of it is the data of the 59 pictures of the
kinds 7 to 65, and the rest is the descriptors of the pictures and the code that puts the
picture on the canvas) and 616 bytes of RAM, and the left half 2,592 bytes of flash and 240
bytes of RAM. The 40 pictures of the kinds 26 to 65 alone added 114,616 bytes of flash to
the right half and nothing to its RAM.

**Status.** The table of the layout and its defaults (taken from the options of the
Corne of that repository), the ranges (those of the turn, the mix and the piece too), the
change event, the flash records of the ids past the first 32, and the sync of only what the
peripheral does not hold have host tests with a fake clock
(`tests/unit/test_display_settings.py`, 50 runs); the display driver, the work queues and
the settings backend are stand-ins there. The firmware of both halves builds in that CI.
The first version of the layout (the widgets moving, the animation changing while the
keyboard runs, the copy of the right screen to the peripheral half) was used on a real
Corne, and its owner reported that it worked, without detail about each operation. What
came after it has not been tried on a real keyboard yet: the turn, the piece and the mix
of the picture (settings 112 to 115), the conversion of the coordinates of a turned
picture, and the 37 pictures left of the new kinds (7 to 65).

### ZMK Studio requests for the display

`CONFIG_KEYPAW_DISPLAY_SETTINGS_STUDIO` (on by default when ZMK Studio's RPC is on
the build, so on the central half) adds three requests to the `keymap` subsystem,
in the same [lenerdz/zmk-studio-messages](https://github.com/lenerdz/zmk-studio-messages)
fork as the colour ones (see [ZMK Studio requests](#zmk-studio-requests) for how
to use the fork). Like the keymap requests, all of them need the keyboard to be
unlocked.

| Request | Id | Answer | What it does |
|---|---|---|---|
| `get_display_info` | 106 | `DisplayInfo`: `settings`, each with `id`, `value`, `min`, `max` and `default_value` | lists the settings |
| `set_display_setting` | 107 | `OK`, `INVALID_ID` or `INVALID_VALUE` | sets one setting |
| `restore_display_defaults` | 108 | `DisplayInfo`, already restored | puts every setting back to its default |

There is no save or discard step, and no notification: a change is applied at
once and written to flash a few seconds later. A client has to use the `min`, `max` and `default_value` that
`get_display_info` reports, because they depend on the firmware (the list itself changes with the
options of the build: fifteen settings, or more with the OLED layout). The list has no
fixed size: `settings` is encoded by a callback, one entry at a time, so a firmware
lists as many settings as it has (the module keeps at most 64, `KP_DISPLAY_SETTING_CAP`).
A firmware without these handlers
answers them with `RPC_NOT_FOUND`, which is how a client can tell that the
feature is missing. ZMK Studio's "Restore Stock Settings" also puts the settings
back to their defaults.

**Status.** The logic of the settings (ranges, flash records, the contrast of each
half, when the screen goes off, the limits of the LEDs, the dimming and the pixel shift,
the command for the peripheral) has host tests with a fake clock
(`tests/unit/test_display_settings.py`, `tests/unit/test_idle_policy.py` and
`tests/unit/test_idle_leds.py`); the display driver, the work queues and the settings
backend are stand-ins there. It builds in the CI of
[lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk). It has not been tried on
a real keyboard yet: the contrast, the screen and the lights going off in each mode,
the copy to the peripheral half and the three requests (the status of the OLED layout is in
[OLED layout](#oled-layout)).

### ZMK Studio requests for the live status and the firmware information

`CONFIG_KEYPAW_DEVICE_STATUS_STUDIO` (on by default when ZMK Studio's RPC is on the
build, so on the central half) adds two read only requests to the `keymap` subsystem,
in the same [lenerdz/zmk-studio-messages](https://github.com/lenerdz/zmk-studio-messages)
fork as the others (see [ZMK Studio requests](#zmk-studio-requests) for how to use the
fork). Like the keymap requests, both need the keyboard to be unlocked. They change
nothing and keep nothing: there is no save step and no notification, so a client that
wants a live view asks again and again.

| Request | Id | Answer | What it says |
|---|---|---|---|
| `get_live_status` | 109 | `LiveStatus` | what the screens draw right now |
| `get_firmware_info` | 110 | `FirmwareInfo` | what the firmware is: it does not change while the keyboard runs, so a client asks once |

`LiveStatus` has these fields. Every value is the central's, read straight from ZMK on
each request (nothing is copied or kept), except two that the central knows about the
other half: whether it is connected and its battery level.

| Field | Meaning |
|---|---|
| `battery_central` | the battery of the central half, from 0 to 100; 255 when there is no reading (a build without `CONFIG_ZMK_BATTERY_REPORTING`) |
| `battery_peripheral` | the battery of the peripheral half, from 0 to 100; 255 when it has not reported (this needs `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`) |
| `peripheral_connected` | the central sees the peripheral |
| `layer_index` and `layer_name` | the highest active layer: its index in the keymap order, and its name (empty when it has none, and cut to `CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN` characters) |
| `wpm` | the words per minute now (0 in a build without `CONFIG_ZMK_WPM`) |
| `output` | where the keys go, ZMK's selected endpoint: `LIVE_OUTPUT_NONE`, `LIVE_OUTPUT_USB` or `LIVE_OUTPUT_BLE` |
| `ble_profile` and `ble_state` | the selected Bluetooth profile (from 0) and its state: `LIVE_BLE_OPEN` (no host paired, advertising), `LIVE_BLE_PAIRED` (paired, host not connected) or `LIVE_BLE_CONNECTED` |
| `usb_state` | `LIVE_USB_NONE`, `LIVE_USB_POWERED` (power, but the host has not set the keyboard up) or `LIVE_USB_HID` (the host uses it) |
| `uptime_s` | seconds since the keyboard started |

`FirmwareInfo` has these fields:

| Field | Meaning |
|---|---|
| `keyboard` and `board` | `CONFIG_ZMK_KEYBOARD_NAME` and `CONFIG_BOARD` |
| `zephyr` | the version of Zephyr (`KERNEL_VERSION_STRING`) |
| `build_id`, `build_branch`, `build_number` and `build_date` | the commit (7 characters), the branch and the number of the CI run that built the firmware, and the day (UTC). CMake takes them from `GITHUB_SHA`, `GITHUB_REF_NAME` and `GITHUB_RUN_NUMBER`, which GitHub Actions sets (also inside the build container), and writes `kp_build_info.h` from `src/kp_build_info.h.in` at every configure. A build outside that CI has them empty, and the number is 0 |
| `flash_total` and `flash_used` | the bytes of the code partition (`zephyr,code-partition` of the devicetree), and the bytes of it that the program takes (`_flash_used` of the linker script, the number the build prints as FLASH) |
| `ram_total` and `ram_used` | the bytes of the RAM (`zephyr,sram`), and the bytes of it that the program takes as soon as it starts (`_image_ram_size`, the number the build prints as RAM: data, bss, noinit, stacks and the heap) |

The strings are cut to fit what the protocol fork allows (`keymap.options.in`). Each
request, like any other, restarts the idle timer of ZMK Studio's lock (when
`CONFIG_ZMK_STUDIO_LOCKING` is on), so a client that asks all the time keeps the keyboard
unlocked. A firmware without these handlers answers them with `RPC_NOT_FOUND`, which is how
a client can tell that the feature is missing. The numbers of flash and RAM are the central's:
the peripheral half is another firmware, and ZMK Studio cannot ask it anything.

**Status.** Both handlers were compiled and run on a computer against the `rpc.h` of ZMK
and the nanopb code that the protocol fork generates, with stand-ins for ZMK and Zephyr
(the values, the cut strings, the 255 of a battery that was not read and the bytes that go
over the wire, both ways); that check is not kept in this repository. The firmware builds
in the CI of [lenerdz/corne-zmk](https://github.com/lenerdz/corne-zmk). With these requests
the central half grew by 1,080 bytes of flash and nothing of RAM in that CI (FLASH from
437,524 to 438,604 bytes, RAM at 153,752 bytes), and the log of that build prints the two
numbers that `get_firmware_info` has to report for it: FLASH 438,604 B and RAM 153,752 B. It
has not been tried on a real keyboard yet: every number it reports, and in particular the flash and RAM (taken
from symbols of the linker that were not used before), the build information in the CI, the
state of the Bluetooth profile and the battery of the peripheral half.

## Built-in components


This ZMK module is designed to be extensible: you can implement effects and
conditions in your own custom module without having to modify code in this
module. Overlays and triggers are configured entirely in devicetree from those.
We also aim to provide a good out-of-the-box experience with a number of
built-in effects, overlays and conditions, which you can just use without having
to touch any C code.

### Built-in effects

These effects are grouped by rendering/state family: static fields (`solid`,
`static`), global oscillators (`breathe`, `spectrum`), moving spatial fields
(`rainbow`, `band`), reactive effects (`reactive`, `ripple`), and stochastic
effects (`rain`, `starlight`, `heatmap`, `digital_rain`).

Every animation below is rendered from ZMK's `native_sim//zmk_test_mock`
"board". The links in the table point to the [gallery
site](https://gudzpoz.github.io/zmk-rgb-matrix/), where each variant sits next
to a copy-and-pastable devicetree snippet that produced it.

<!-- No GIFs inside links: GitHub hides them for people with Accessibility.Motion = Disabled -->

<table>
<thead><tr><th>Effect</th><th>Description</th><th>Attributes</th></tr></thead>
<tbody>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid">solid</a></code></td>
    <td>One flat colour across every LED, or a static hue gradient swept across the board.</td>
    <td><code>axis</code> = <code>none</code> (flat colour) |
        <code>vertical</code> (hue sweep across y) |
        <code>horizontal</code> (hue sweep across x)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid.gif" width="200" alt="Solid" title="Solid"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid-vertical">vertical</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid-vertical.gif" width="200" alt="Solid (vertical)" title="Solid (vertical)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#solid-horizontal">horizontal</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/solid-horizontal.gif" width="200" alt="Solid (horizontal)" title="Solid (horizontal)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe">breathe</a></code></td>
    <td>Board-wide fade or a position-aware oscillator over one period.</td>
    <td><code>mode</code> = <code>brightness</code> (BREATHING) |
        <code>river</code> (RIVERFLOW) | <code>hue</code> (HUE_BREATHING) |
        <code>pendulum</code> (HUE_PENDULUM) | <code>wave</code> (HUE_WAVE)<br>
        <code>hue-amplitude</code> (hue swing in degrees for the hue modes, default 45)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-brightness">brightness</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-brightness.gif" width="200" alt="Breathe (brightness)" title="Breathe (brightness)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-river">river</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-river.gif" width="200" alt="Breathe (river)" title="Breathe (river)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-hue">hue</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-hue.gif" width="200" alt="Breathe (hue)" title="Breathe (hue)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-pendulum">pendulum</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-pendulum.gif" width="200" alt="Breathe (pendulum)" title="Breathe (pendulum)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#breathe-wave">wave</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/breathe-wave.gif" width="200" alt="Breathe (wave)" title="Breathe (wave)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#spectrum">spectrum</a></code></td>
    <td>Whole board cycles the hue wheel globally (QMK <code>CYCLE_ALL</code>).</td>
    <td>&mdash;</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#spectrum">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/spectrum.gif" width="200" alt="Spectrum" title="Spectrum">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive">reactive</a></code></td>
    <td>Lights a pressed key and fades it back; the lit shape is configurable.</td>
    <td><code>spread</code> = <code>point</code> (key only) |
        <code>disc</code> (filled circle) | <code>cross</code> (row + column) |
        <code>nexus</code> (all but the cross, radial falloff)<br>
        <code>spread-radius</code> (disc/nexus radius, in layout units; for nexus it is the band width measured outward from the excluded cross)<br>
        <code>background-brightness</code> (unlit-LED brightness, percent)<br>
        <code>multi</code> (accumulate several presses instead of the latest)<br>
        <code>palette</code> = <code>solid</code> (user hue) |
        <code>gradient</code> (position-based hue) |
        <code>complement</code> (unlit floor keeps the hue, a pressed key flashes the opposite hue)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-point">point</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-point.gif" width="200" alt="Reactive (point)" title="Reactive (point)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-disc">disc</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-disc.gif" width="200" alt="Reactive (disc)" title="Reactive (disc)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-cross">cross</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-cross.gif" width="200" alt="Reactive (cross)" title="Reactive (cross)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-nexus">nexus</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-nexus.gif" width="200" alt="Reactive (nexus)" title="Reactive (nexus)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-gradient">gradient</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-gradient.gif" width="200" alt="Reactive (gradient)" title="Reactive (gradient)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-complement">complement</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-complement.gif" width="200" alt="Reactive (complement)" title="Reactive (complement)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#reactive-multi">multi</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/reactive-multi.gif" width="200" alt="Reactive (multi)" title="Reactive (multi)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#ripple">ripple</a></code></td>
    <td>Radial ripples expand from each pressed key over an unlit background.</td>
    <td><code>background-brightness</code> (unlit background brightness, percent)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#ripple">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/ripple.gif" width="200" alt="Ripple" title="Ripple">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow">rainbow</a></code></td>
    <td>Position-aware rainbow gradient with selectable spatial basis and direction.</td>
    <td><code>basis</code> = <code>x</code> | <code>y</code> | <code>radial</code> |
        <code>pinwheel</code> | <code>spiral</code> | <code>chevron</code> |
        <code>flag</code>
        (spatial coordinate the colour maps onto; <code>flag</code> is a
        left-right sweep skewed per row so it undulates)<br>
        <code>direction</code> = <code>out</code> | <code>in</code> (reverse) |
        <code>dual</code> (two centres) | <code>bloom</code> (mirror each half, with basis x)<br>
        <code>palette</code> = <code>rainbow</code> (full hue wheel) |
        <code>solid</code> (single hue, travelling brightness band)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-x">x</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-x.gif" width="200" alt="Rainbow (x)" title="Rainbow (x)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-y">y</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-y.gif" width="200" alt="Rainbow (y)" title="Rainbow (y)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-radial">radial</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-radial.gif" width="200" alt="Rainbow (radial)" title="Rainbow (radial)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-pinwheel">pinwheel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-pinwheel.gif" width="200" alt="Rainbow (pinwheel)" title="Rainbow (pinwheel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-spiral">spiral</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-spiral.gif" width="200" alt="Rainbow (spiral)" title="Rainbow (spiral)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-chevron">chevron</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-chevron.gif" width="200" alt="Rainbow (chevron)" title="Rainbow (chevron)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-flag">flag</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-flag.gif" width="200" alt="Rainbow (flag)" title="Rainbow (flag)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-in">in</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-in.gif" width="200" alt="Rainbow (in)" title="Rainbow (in)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-dual">dual</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-dual.gif" width="200" alt="Rainbow (dual)" title="Rainbow (dual)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-bloom">bloom</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-bloom.gif" width="200" alt="Rainbow (bloom)" title="Rainbow (bloom)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rainbow-solid">solid</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rainbow-solid.gif" width="200" alt="Rainbow (solid)" title="Rainbow (solid)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band">band</a></code></td>
    <td>A single-hue board with a moving saturation or brightness band.</td>
    <td><code>channel</code> = <code>sat</code> (fades saturation) |
        <code>val</code> (fades brightness)<br>
        <code>shape</code> = <code>linear</code> (scroll) |
        <code>pinwheel</code> (rotate) | <code>spiral</code> (wind outward)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-linear">saturation, linear</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-linear.gif" width="200" alt="Band (saturation, linear)" title="Band (saturation, linear)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-pinwheel">saturation, pinwheel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-pinwheel.gif" width="200" alt="Band (saturation, pinwheel)" title="Band (saturation, pinwheel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-saturation-spiral">saturation, spiral</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-saturation-spiral.gif" width="200" alt="Band (saturation, spiral)" title="Band (saturation, spiral)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#band-value-linear">value, linear</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/band-value-linear.gif" width="200" alt="Band (value, linear)" title="Band (value, linear)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#static">static</a></code></td>
    <td>Fixed per-LED colour map (QMK <code>ALPHAS_MODS</code> and similar art); each LED
        takes a colour from the <code>slot-colors</code> array (by colour slot) or, without it,
        from the <code>led-colors</code> array (by LED).</td>
    <td><code>slot-colors</code> (RGB values, one per colour slot; needs <code>slot-count</code> on the
        <code>keypaw,rgb-matrix</code> node), <code>layer-id</code> (makes the map editable at run time, see
        <a href="#colour-slots-and-editable-colour-maps">Colour slots and editable colour maps</a>),
        <code>led-colors</code> (RGB values, one per LED by chain index, used only without
        <code>slot-colors</code>; LEDs past the end stay off)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#static">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/static.gif" width="200" alt="Static" title="Static">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain">rain</a></code></td>
    <td>Randomly lit keys with random colours (QMK <code>PIXEL_RAIN</code>, <code>PIXEL_FLOW</code>,
        <code>RAINDROPS</code>, <code>JELLYBEAN_RAINDROPS</code>, <code>PIXEL_FRACTAL</code>).</td>
    <td><code>mode</code> = <code>pixel</code> (random keys, random hues) |
        <code>drops</code> (nudges one random key's hue each step; the board stays lit, so it needs a saturated colour) |
        <code>jellybean</code> (also randomises saturation) |
        <code>flow</code> (cursor along chain) |
        <code>fractal</code> (single hue pulses from centre)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-pixel">pixel</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-pixel.gif" width="200" alt="Rain (pixel)" title="Rain (pixel)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-flow">flow</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-flow.gif" width="200" alt="Rain (flow)" title="Rain (flow)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-drops">drops</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-drops.gif" width="200" alt="Rain (drops)" title="Rain (drops)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-jellybean">jellybean</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-jellybean.gif" width="200" alt="Rain (jellybean)" title="Rain (jellybean)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#rain-fractal">fractal</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/rain-fractal.gif" width="200" alt="Rain (fractal)" title="Rain (fractal)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight">starlight</a></code></td>
    <td>LEDs turn on and off at random at varying brightness, keeping the user colour.</td>
    <td><code>smooth</code> (ramp brightness instead of switching) |
        <code>dual-hue</code> (jitter hue &plusmn;30) |
        <code>dual-sat</code> (jitter saturation &plusmn;30)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight.gif" width="200" alt="Starlight" title="Starlight"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-smooth">smooth</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-smooth.gif" width="200" alt="Starlight (smooth)" title="Starlight (smooth)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-dual-hue">dual hue</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-dual-hue.gif" width="200" alt="Starlight (dual hue)" title="Starlight (dual hue)"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#starlight-dual-sat">dual sat</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/starlight-dual-sat.gif" width="200" alt="Starlight (dual sat)" title="Starlight (dual sat)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap">heatmap</a></code></td>
    <td>Per-key heat map of recent keypresses, decaying over time (QMK <code>TYPING_HEATMAP</code>).</td>
    <td><code>decrease-delay-ms</code> (ms idle before one shade of heat is lost)<br>
        <code>spread</code> (radius heating neighbours, layout units)<br>
        <code>area-limit</code> (per-press cap on heat a neighbour receives)<br>
        <code>increase-step</code> (shades added per keypress)<br>
        <code>slim</code> (heat only the pressed key, not neighbours)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/heatmap.gif" width="200" alt="Heatmap" title="Heatmap"><br>
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#heatmap-slim">slim</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/heatmap-slim.gif" width="200" alt="Heatmap (slim)" title="Heatmap (slim)">
</td></tr>
<tr><td><code><a href="https://gudzpoz.github.io/zmk-rgb-matrix/#digital-rain">digital_rain</a></code></td>
    <td>Matrix-style falling columns: a head falls per column leaving a fading green tail.</td>
    <td><code>color</code> (tail hue; green default) |
        <code>duration</code> (fall speed; shorter is faster)</td></tr>
<tr><td colspan="3">
<a href="https://gudzpoz.github.io/zmk-rgb-matrix/#digital-rain">default</a>: <img src="https://gudzpoz.github.io/zmk-rgb-matrix/digital-rain.gif" width="200" alt="Digital Rain" title="Digital Rain">
</td></tr>
</tbody>
</table>

Every effect accepts `color` and `duration`. Selectable effects also accept
`overlays` and `no-overlays`. `RGB_EFF`/`RGB_EFR` cycle through all enabled
selectable effects; private overlay effects are not part of that list.

### Built-in conditions and overlays

| Component | Compatible | Notes |
|---|---|---|
| Always-on condition | `keypaw,rgb-condition-always` | / |
| Caps Lock condition | `keypaw,rgb-condition-caps-lock` | Active while the host reports Caps Lock |
| Layer condition | `keypaw,rgb-condition-layer` | Active while a keymap layer is active |
| BLE condition | `keypaw,rgb-condition-ble` | Central-only; matches the active BLE profile index and connection state |
| Latch condition | `keypaw,rgb-condition-latch` | Stateful; toggled by the behavior nested under it |
| And condition | `keypaw,rgb-condition-and` | All listed conditions active |
| Or condition | `keypaw,rgb-condition-or` | Any listed condition active |
| Overlay toggle | `keypaw,behavior-rgb-overlay-toggle` | Child of `keypaw,rgb-condition-latch`. Node name ≤ 8 chars |
| Battery gauge | `keypaw,rgb-overlay-battery` | Level bar from this half's charge |

The generic `keypaw,rgb-overlay` compositor is the other overlay kind: it
renders one effect while its condition holds.

## Custom effects and conditions

Third-party modules can add effects, conditions and overlays without editing
this module; overlays and triggers are then configured in devicetree from them.
The full authoring guide is [`docs/extending.md`](./docs/extending.md).

As a quick taste, a third-party effect module includes the public API as
`<zmk/rgb_matrix.h>`, provides a binding that includes
`keypaw,rgb-matrix-effect-common.yaml`, embeds
`struct kp_rgb_effect_common_config` and `struct kp_rgb_effect_common_data` as
the first member of the effect's config and data structures, and instantiates
each enabled devicetree node with `KP_RGB_EFFECT_DEFINE()`.

`modules/zmk-rgb-effect-example` in the parent
[`keypaw48-zmk`](https://github.com/gudzpoz/keypaw48-zmk/tree/main/modules/zmk-rgb-effect-example)
repository is a complete separate-module example. It demonstrates the required
binding, data layout, renderer, and `&kprgb` child node without using internal
matrix headers.

## License

MIT. See [LICENSE](./LICENSE).

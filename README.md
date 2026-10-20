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
`idle-brightness` while idle.

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
| `get_led_info` | 100 | `LedInfo`: `slot_count`, `layer_ids` (up to 8) and `unsaved_changes` | tells what can be edited |
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
to 8 maps and `set_led_colors` carries up to 16 slots.

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

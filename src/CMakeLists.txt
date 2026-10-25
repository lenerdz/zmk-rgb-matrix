# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_LED_STRIP_COMPOSITE drivers/led_strip_composite.c)

# ZMK app headers
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)

zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_MATRIX
  # Engine
  rgb_matrix.c
  rgb_utils.c
  rgb_instance.c
  rgb_workqueue.c
  rgb_conditions.c
  rgb_control_start.c
  # Pure colour math
  rgb_color.c
  # Persisted-record codec
  rgb_persist.c

  # Built-in stuff: zero cost when there's no DTS definitions
  conditions/layer.c
  conditions/always.c
  conditions/latch.c
  conditions/and.c
  conditions/or.c
  overlays/overlay.c
  overlays/battery.c
  behaviors/overlay_toggle.c
)

# Non-zero cost when there's no DTS definitions:
# - Caps Lock pulls in ZMK_HID_INDICATORS.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_CONDITION_CAPS_LOCK conditions/caps_lock.c)

# BLE host-profile state exists only on the central.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_CONDITION_BLE conditions/ble.c)

zephyr_library_sources_ifdef(CONFIG_KEYPAW_BEHAVIOR_RGB_MATRIX
  # controller and effect persistence
  behavior_rgb_matrix.c
  rgb_settings.c
  # editable colour maps: registry, live edits, flash records and their codec
  rgb_maps.c
  rgb_map_codec.c
  # global oscillators and static fields
  effects/solid.c
  effects/breathe.c
  effects/spectrum.c
  effects/static.c
  # reactive fields
  effects/reactive.c
  effects/ripple.c
  # moving spatial fields
  effects/rainbow.c
  effects/band.c
  # stochastic fields
  effects/rain.c
  effects/starlight.c
  effects/heatmap.c
  effects/digital_rain.c
)

# The change event is declared in include/zmk/events/rgb_matrix_changed.h; its
# implementation stays out of the build when nothing raises it.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_MATRIX_CHANGE_EVENT events/rgb_matrix_changed.c)

# Trigger table: central-only, because a trigger's condition may read keymap
# state, which does not exist on a peripheral.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_TRIGGERS rgb_triggers.c)

# Split state sync: central-only, because it needs the transport's connected
# source list, which only exists on the central.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_SPLIT_SYNC rgb_split_sync.c)

# Display (OLED) settings: contrast, when the screen and the lights go off, toast,
# anti burn-in. Both halves keep them; the central also pushes them to its
# peripheral, through the RGB behavior and the split sync above. idle_policy.c
# counts the idle time that the screen and the LEDs follow.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_DISPLAY_SETTINGS display_settings.c idle_policy.c)

# The event that tells the screen widgets that the OLED layout changed is declared
# in include/zmk/events/display_settings_changed.h; display_settings.c raises it.
zephyr_library_sources_ifdef(CONFIG_KEYPAW_OLED_LAYOUT events/display_settings_changed.c)

# ZMK Studio access to the colour maps, to the display settings and to the live
# status and firmware information: central-only, because ZMK Studio is.
# studio_rpc.c, studio_rpc_display.c and studio_rpc_status.c include the headers
# that ZMK generates from the .proto files (<build>/proto/zmk/*.pb.h) as part of
# its own `app` target, so this library needs that directory on its include path
# and must not compile before `app` has generated them.
if(CONFIG_KEYPAW_RGB_MAPS_STUDIO OR CONFIG_KEYPAW_DISPLAY_SETTINGS_STUDIO OR
   CONFIG_KEYPAW_DEVICE_STATUS_STUDIO)
  zephyr_library_include_directories(${CMAKE_BINARY_DIR})
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} app)
endif()
zephyr_library_sources_ifdef(CONFIG_KEYPAW_RGB_MAPS_STUDIO studio_rpc.c)
zephyr_library_sources_ifdef(CONFIG_KEYPAW_DISPLAY_SETTINGS_STUDIO studio_rpc_display.c)

# The firmware information that ZMK Studio reads says which build it is: the commit,
# the branch and the number of the CI run (GitHub Actions sets GITHUB_SHA,
# GITHUB_REF_NAME and GITHUB_RUN_NUMBER, also inside the build container) and the
# day it was built. A build outside the CI has them empty, and the number is 0. The
# values are cleaned up, because they go inside C string literals and numbers.
if(CONFIG_KEYPAW_DEVICE_STATUS_STUDIO)
  set(KP_BUILD_ID "")
  if(DEFINED ENV{GITHUB_SHA})
    string(REGEX REPLACE "[^0-9a-fA-F]" "" KP_BUILD_ID "$ENV{GITHUB_SHA}")
    string(SUBSTRING "${KP_BUILD_ID}" 0 7 KP_BUILD_ID)
  endif()

  set(KP_BUILD_BRANCH "")
  if(DEFINED ENV{GITHUB_REF_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9._/+-]" "_" KP_BUILD_BRANCH "$ENV{GITHUB_REF_NAME}")
  endif()

  set(KP_BUILD_NUMBER 0)
  if(DEFINED ENV{GITHUB_RUN_NUMBER} AND "$ENV{GITHUB_RUN_NUMBER}" MATCHES "^[0-9]+$")
    set(KP_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
  endif()

  string(TIMESTAMP KP_BUILD_DATE "%Y-%m-%d" UTC)

  configure_file(kp_build_info.h.in ${CMAKE_CURRENT_BINARY_DIR}/kp_build_info.h @ONLY)
  zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR})
  zephyr_library_sources(studio_rpc_status.c)
endif()

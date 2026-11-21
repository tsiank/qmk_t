# Qmknrf5 shared features

Set `NRF5_CONFIG_ENABLE = yes` and (for the music/control transports)
`MUSIC_STREAM_ENABLE = yes` in an nRF5 keyboard's rules.mk. The build system
loads rules.mk here after all keyboard/keymap overrides. Periodic tasks run
from the nRF5 main loop; no keymap housekeeping hook is required.

Keep hardware configuration, matrix and LED maps, indicator segments and
keymaps in keyboards/<name>. Reuse nrf5_lighting.h and nrf5_midi_typing.h from
process_record_user for keymap-specific custom toggle codes. The shared
renderer reads QMK's g_led_config; it does not contain ciank67 LED indices.

See util/ciank67_music/WinUI/README.md for the full integration and layout
profile format, including optional RGB effects and the VIA matrix query.

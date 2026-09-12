# Device settings

Home has four destinations: Look around, Pokédex, My stamps and Sound & screen.
Settings exposes Sound volume, Quiet mode, Screen light, Save and go back, and Undo and go back.

- Defaults: volume 60%, brightness 60%, mute off.
- Volume ranges from 0–100%, brightness from 10–100%, in 10% steps. Screen-off
  remains a separate action so changing brightness cannot hide the controls.
- UP/DOWN selects a row. OK enters level editing; UP raises and DOWN lowers
  the value. OK finishes editing and previews the buddy's cry (Pikachu if no
  buddy) for volume. Mute toggles with OK and retains the selected volume.
- Changes preview in RAM. Save and go back commits one small settings blob in a
  worker and returns Home only after successful write/readback. Undo and go back or
  hold OK restores the saved preferences. Inputs are locked during the save.
- An unchanged save does not write to flash. A failed save stays on Settings
  with a retry message; unavailable workers are treated as save failures.
- Settings remains awake during editing and saving. Home's screen-off and
  consumed wake press remain unchanged. Wake and battery updates use the
  active preference instead of returning to 100%. Low battery caps it at 30%
  and never raises a lower user brightness or changes the saved preference.

`user_settings` owns defaults, bounded adjustments, brightness policy and a
versioned 12-byte CRC-protected codec. `bsp_settings_store` uses the independent
NVS namespace `preferences`, key `settings_v1`. Missing values load defaults
without writing. Invalid/unreadable values fall back to defaults and show a
message; a deliberate Save replaces them. Collection/place formats and keys
are unchanged. A readback failure after commit is reported as a failure even
though the write may already be durable; retry is safe.

The audio worker remains the sole owner of the codec. An atomic preference
word carries volume and mute across tasks. Muted/zero-volume requests do not
play, setting either stops pending audio, and an active cry checks preferences
between 20 ms PCM writes. Cancellation still drains silence to avoid stale
audio reappearing on unmute. Physical speaker loudness and panel brightness
need device acceptance; percentage settings are control values.

Host tests cover the domain and actual NVS adapter with injected failures,
production Settings button/completion handlers with hardware stubs, and the
audio worker's volume/mute handling. Production LVGL fixtures exercise Home's
fourth destination and all settings rows, editing, extremes, mute, defaults,
save/read errors, saving and low battery.

For an opt-in device check, build with `CITY_SETTINGS_SMOKE=ON` (other smoke
options off). It pauses gameplay timers and skips input registration, exercises
cancel, mute and brightness through the production handlers, commits test
preferences via the real worker, independently reloads them, and restores the
original preferences. Back up NVS before running it; interrupted diagnostics
may leave test preferences saved. Install a normal build with this option OFF
afterward. This verifies firmware/hardware execution, not subjective loudness
or physical button operation.

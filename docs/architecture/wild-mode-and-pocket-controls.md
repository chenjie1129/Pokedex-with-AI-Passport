# Wild Mode and pocket controls

Wild Mode is offered only after a successful scan returns too little evidence
for a place encounter. A failed scan and an ambiguous place match do not grant
Wild encounters. The player chooses OK to search or UP to return home.

The wild pool selects Bulbasaur (70%) or Squirtle (30%) from an injected random
seed. It does not create a saved place. Collection records use reserved place ID
0, displayed as WILD; normal saved places start at 1.

## Durable cooldown

Starting a wild search reserves the opportunity and records the species as seen
in one durable bestiary write, before the encounter is shown. Leaving or losing
therefore still consumes that opportunity. The cooldown is 30 minutes of device
uptime. On restart while the saved cooldown is active, a fresh 30-minute wait is
required: the device has no trusted wall clock across power loss. The Wild Mode
screen states this policy and shows the remaining time.

Bestiary schema 5 retains the existing 156-byte wire length, record offsets,
sequence and checksum. Byte 76 holds the cooldown flag. Schemas 1–4 migrate with
that flag clear, retaining existing collection data. An expired cooldown clears
through the flash worker when no encounter, capture, scan or save is active;
failed clears retry after 30 seconds. Normal collection writes preserve the flag.
No timestamp or wireless identifiers are persisted.

Back up NVS and the app before installation. Downgrading to schema-4 firmware
requires restoring its corresponding NVS backup as well. Never erase user data
or flash bootloader, partition table or recovery merely to install this app.

## Screen and battery

- Hold UP on Home to turn the backlight off.
- Static screens turn off after 60 seconds without input, or 15 seconds at 10%
  battery or below. Active scans, encounters, captures and saves stay awake.
- Any button press wakes the display. The click/long event belonging to that
  press is consumed; the next independent press works normally.
- This is backlight-off, not deep sleep. MCU, button sampling and cooldown clock
  keep running. The UI update timer slows from 33 ms to 1000 ms while off.
- The existing CW2017 adapter reads charge in a background task every 30 seconds.
  Missing/invalid readings show BAT --, never an invented percentage or USB
  charging claim. At 10% or below the header shows LOW and the backlight dims.
  At 5% or below new explorations are blocked; current captures/saves can finish.
- Flash and I2C work run outside LVGL callbacks. A one-item queue delivers battery
  readings; the existing save worker serializes bestiary mutations.

## Validation

Run `./tools/test-host.sh`. `test_pocket_features` exercises failed reservation,
reboot/abandon cooldown, duplicate settlement, normal catches during cooldown,
failed expiry writes, schema-4 wire migration, invalid flag rejection, pool
selection and idle/wake/battery boundary behavior under sanitizers.

Physical acceptance still needs the actual display and buttons: verify a known
place capture, Wild Mode in a sparse environment, leaving/re-entering and
restarting during cooldown, manual/automatic screen-off, consumed wake press,
and battery readings while unplugged. Host tests do not establish battery gauge
calibration or power consumption.

The first device run detected CW2017 revision 0x0F but returned invalid SOC with
the inherited profile-free driver. The battery adapter is updated from the
[official board driver at f75873f1](https://github.com/FoloToy/ai-passport/blob/f75873f1aab24ac4c0ba9394c131669f66cce650/components/bsp/src/bsp_battery.c),
including the stock Youteli 520mAh profile, readback verification, prescribed
sleep/restart sequence and bounded SOC readiness wait. This profile assumes the
original board battery; a replacement cell needs its corresponding profile.
`test_battery_driver` compiles the production adapter against an injected I2C
register bus to check profile writes occur only during sleep, a matching profile
is not rewritten, readback/write failures prevent readiness, and SOC timeout
cleans up the device handle. It does not validate cell calibration.

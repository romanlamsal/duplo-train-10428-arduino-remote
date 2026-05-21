# LEGO Duplo 10428 — ESP32 / BLE notes

Working notes for controlling the 10428 steam train hub from an ESP32 over BLE,
without depending on Legoino (which doesn't recognise this newer hub variant).

## What works today

- Detect, connect, subscribe to notifications.
- Drive the motor at signed speeds (-100..100) via serial input.

Hardware: ESP32 NodeMCU (ESP-WROOM-32, "DevKit V1"), NimBLE-Arduino.
Sketch: `duplotrain.ino`.

## Hub identification

The 10428 is a new variant of the Duplo Train Hub. Legoino / node-poweredup
filter by manufacturer-data byte index 3, which used to be `0x20`. The 10428
reports **`0x21`**.

Observed advertisement:

```
name = "DUPLO Train   "
mfg  = 97 03 00 21 02 ff 01 00
       ^^^^^      LEGO company ID (little-endian 0x0397)
             ^^   button state
                ^^ hub type = 0x21  ← NEW (old Duplo train = 0x20)
                   ^^ capabilities, then status bits
```

Filter in firmware: accept `mfg[3] == 0x20 || mfg[3] == 0x21`.

## BLE UUIDs (same as all LPF2 hubs)

| Purpose | UUID |
|---|---|
| Service | `00001623-1212-efde-1623-785feabcd123` |
| Characteristic (write + notify) | `00001624-1212-efde-1623-785feabcd123` |

All commands go to that single characteristic. Subscribe to notifications on
it to receive port announcements, sensor data, command feedback.

## Discovered port map (from Hub-Attached-I/O notifications)

Sent by hub right after subscribe. Format:
`[len, 0x00, 0x04, portId, 0x01 (attached), devTypeLo, devTypeHi, ...]`.

| Port | Device type | Identity |
|---|---|---|
| `0x32` (50) | `0x29` = 41 | **Motor** (DUPLO_TRAIN_BASE_MOTOR — same ID as old hub) |
| `0x33` (51) | `0x5b` = 91 | unknown — **candidate: speaker** |
| `0x34` (52) | `0x5a` = 90 | unknown |
| `0x35` (53) | `0x14` = 20 | unknown — **candidate: RGB head light** |
| `0x36` (54) | `0x2c` = 44 | **Speedometer** (same ID as old hub) |

The old Duplo train used ports 0/1/18/19 with device IDs 41/42/43/44.
10428 renumbered ports into the `0x3x` range AND introduced three new
device types (20, 90, 91). Speaker (42) and color sensor (43) are absent
from this list, so at least one of the new devices replaces each.

## Wire format

LWP3 envelope written to the LPF2 characteristic:

```
[ length,        // total bytes including this one (1 byte for <128)
  0x00,          // hub_id, always 0
  msg_type,      // 0x04 attached-IO, 0x41 input-format, 0x81 port-output, ...
  payload... ]
```

### Motor power (confirmed working)

```
0x08 0x00 0x81 0x32 0x11 0x51 0x00 <speed>
 len  hub PortOut port start+fb WrDir mode  int8 (-100..100, 0=stop, 0x7f=brake)
```

`start+fb = 0x11` = execute immediately + request command feedback.

### Sound (untested on 10428)

Original Duplo train recipe (from node-poweredup), to send on speaker port:

1. Enable mode-01 notifications on the speaker port:
   `0x0a 0x00 0x41 <port> 0x01 0x01 0x00 0x00 0x00 0x01`
2. Write direct sound id:
   `0x08 0x00 0x81 <port> 0x11 0x51 0x01 <soundId>`

Sound IDs from old firmware (may or may not match 10428):
`BRAKE=3, STATION_DEPARTURE=5, WATER_REFILL=7, HORN=9, STEAM=10`.

## Still to do / sniff

- **Find the speaker port.** Brute-force a HORN command (sound id 9, mode 1)
  on ports `0x33`, `0x34`, `0x35`; listen for which one makes noise.
- **Identify device types 0x14 / 0x5a / 0x5b.** Subscribe to mode 0 on each
  and observe notification payloads — RGB light vs color sensor vs speaker
  vs anything else. Compare patterns: color sensors emit 1-byte color
  index; speaker is write-only; light is write-only.
- **Sound IDs for the new firmware.** Even once we find the speaker port,
  the old 1/3/5/7/9/10 set may have been extended or replaced — enumerate
  `0..31` once port is known.
- **Headlight / brake light control.** The 10428 has visible lights on the
  loco. One of the new device types is presumably an RGB light. LPF2
  RGB-light write recipe: `0x81 <port> 0x11 0x51 <mode> <data>` with
  mode 0 = color index (0..10), mode 1 = absolute RGB.
- **Speedometer reads.** Subscribe with `0x41 0x36 <mode> 0x01 0x00 0x00 0x00 0x01`
  for mode 0 (speed) and mode 1 (count) and decode notifications.
- **Color sensor reads.** Once the new color sensor port is identified,
  same subscribe pattern. The train usually has a sensor pointing down at
  the track to read colored "action" bricks.
- **Hub LED.** Standard LPF2 hubs expose an RGB LED on a fixed port
  (often `0x32` on older hubs — collides here). May be on a different
  internal port on 10428; check for type `0x17` (23 = HUB_LED) in any
  later attached-IO notifications, or after sending a Hub-Properties
  query.
- **Battery level + button events.** Hub Properties messages:
  `0x05 0x00 0x01 0x06 0x05` → request battery, etc.

## Useful references

- LWP3 spec: https://lego.github.io/lego-ble-wireless-protocol-docs/
- node-poweredup source (read for protocol patterns; doesn't yet support 10428):
  https://github.com/nathankellenicki/node-poweredup
  - `src/hubs/duplotrainbase.ts`, `src/devices/duplotrainbase*.ts`
  - `src/consts.ts` for device-type and sound enums
- Legoino (also unaware of 10428):
  https://github.com/corneliusmunz/legoino

## Practical gotchas hit during bringup

- ESP32 brownout detector trips when the BLE radio starts on marginal USB.
  Disable with `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);` at top of
  `setup()`. (Use a better cable too.)
- Random garbage from Serial Monitor on reboot is the ROM bootloader
  chattering at 74880 baud — harmless after `boot` print appears.
- Train auto-disconnects fast if the central doesn't subscribe to
  notifications quickly. nRF Connect "connect" alone isn't enough.
- The official Powered Up app on a nearby phone will steal the train.
  Force-quit it or disable phone Bluetooth while developing.

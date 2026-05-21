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
| `0x33` (51) | `0x5b` = 91 | **TAG** (write-only output; mode 0, single u16. External writes silently rejected with error 0x06 in practice.) |
| `0x34` (52) | `0x5a` = 90 | **EVENTS** trigger + observation port. 3 modes: 0=VERS, 1=EVENTS (2×u16 LE), 2=DEBUG. All commands (sound, light) go through mode 1. Also emits action-brick scan events on the same channel. |
| `0x35` (53) | `0x14` = 20 | **Voltage sensor** (battery). Read-only, 2-byte LE mV reading. |
| `0x36` (54) | `0x2c` = 44 | **Speedometer** (same ID as old hub) |

The old Duplo train used ports 0/1/18/19 with device IDs 41/42/43/44.
10428 renumbered ports into the `0x3x` range and folded the old beeper +
color-sensor + RGB-light into a single bidirectional **EVENTS** port
(`0x34`, device type `0x5a`). Device types `0x5a` (90) and `0x5b` (91)
are not in pybricks `technical-info/assigned-numbers.md`; hub variant
`0x21` is also undocumented there.

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

### EVENTS — sound, light, brick scans (port 0x34 mode 1)

The 10428's beeper, RGB light, and color sensor are all collapsed into a
single bidirectional port: **`0x34` mode 1 ("EVENTS")**. Two 16-bit LE
values per write/read: `(opcode, parameter)`. Recovered by sniffing the
official LEGO DUPLO Interactive Trains app via Android Bluetooth HCI
snoop.

1. Subscribe to EVENTS notifications (also gates writes):
   `0x0a 0x00 0x41 0x34 0x01 0x01 0x00 0x00 0x00 0x01`
2. Write an event:
   `0x0b 0x00 0x81 0x34 0x11 0x51 0x01 <op_lo> <op_hi> <par_lo> <par_hi>`

| Opcode | Parameter | Effect |
|---|---|---|
| `0x0107` | sound id (`0` = horn) | Play sound. **Side effect:** train also flashes the headlight yellow. |
| `0x0104` | colour index | Set headlight colour. Observed palette: `0, 1, 7, 8, 9, 10, 11`. |
| `0xf001` | 0 | Session init. LEGO app sends once at connect; in practice our subscribe is enough and we never need to send this. |

Inbound action-brick scan events appear as `0x45` Port Value Single on
port `0x34`, e.g. the horn brick produces:

```
notify: 08 00 45 34 <op_lo> <op_hi> <par_lo> <par_hi>
```

Captured horn-brick events used opcode `0xa001` (NB: different from the
`0x0107` write-side opcode — read and write opcodes are not symmetric).

## Speedometer (port 0x36 mode 0)

Firmware now subscribes at connect:

```
0x0a 0x00 0x41 0x36 0x00 0x01 0x00 0x00 0x00 0x01
```

Notifications arrive as Port-Value-Single (`0x45`) on port `0x36`:

```
notify: 05 00 45 36 <int8>
```

Confirmed by pushing the train forward/backward by hand (motor unpowered)
and watching the values transition smoothly through zero, e.g.:

```
forward push  : 11, 1f, 32, 36, 38, 39, 3a, 3d, 40, 00      (+17..+64, →0)
backward push : ec, d1, c7, c3, c6, bf, c2, c0, d4, f0, 00  (-20..-65, →0)
```

`onNotify` decodes byte 4 as `int8_t` and stores its sign on
`gObservedDir`. Only the sign is consumed (`Train::observedDirection()`
returns `-1/0/+1`). An earlier int16-fallback branch was removed once the
int8 format was confirmed — re-add if a future hub variant turns out to
emit wider payloads.

## Still to do / sniff

- **Map the other action bricks.** Roll the train over each of the 5
  action bricks (horn, brake, station, water, steam) and record both the
  `0xa001` (or other) read-side opcode for each, and any associated `0x0107`
  write-side sound id by scrubbing through the LEGO app. With the table
  filled in, `Train::honk()` can grow `Train::brake()`, `Train::depart()`, etc.
- **Speedometer mode 1 (count).** Tick counter, not yet decoded.
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

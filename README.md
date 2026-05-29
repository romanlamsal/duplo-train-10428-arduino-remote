# duplo-train-10428-arduino-remote

An ESP32 firmware that turns a handful of pushbuttons into a physical remote
control for the **LEGO DUPLO 10428 steam train**. It mimics the controls of
the official *LEGO DUPLO Interactive Trains* app:

- forward
- backward
- stop
- honk
- cycle the headlight colour

This is a **non-Legoino** implementation. [Legoino](https://github.com/corneliusmunz/legoino)
and [node-poweredup](https://github.com/nathankellenicki/node-poweredup) both
predate the 10428 hub variant and won't even recognise the train on the air;
they filter the advertisement by a hub-type byte that has changed. This sketch
talks the LEGO Wireless Protocol 3 (LWP3) directly over BLE using
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), so it works with
the new hub today and is also a usable starting point if you want to script
the train yourself.

## Hardware

- ESP32 NodeMCU (tested on ESP-WROOM-32 "DevKit V1")
- 5 momentary pushbuttons, each wired between a GPIO and GND
- 1 potentiometer wired between 3V3 and GND, wiper on GPIO 34
  (throttle). 300° pots work fine; ADC spans the full mechanical sweep
  either way.
- LEGO DUPLO 10428 train (the steam train with the new hub variant — see
  [Hub identification](#hub-identification) if you're not sure which one you
  have)

Buttons use the ESP32's internal pull-ups, so no external resistors are
needed. Defaults in [`Buttons.cpp`](Buttons.cpp):

| Function | GPIO | Notes |
|---|---|---|
| Forward   | 12 |  |
| Backward  | 13 |  |
| Stop      | 14 |  |
| Honk      | 27 |  |
| Light     | 26 |  |
| Throttle  | 34 | ADC1, input-only, BLE-safe |

## Build & flash

Open `duplo-train-10428-arduino-remote.ino` in the Arduino IDE (or
`arduino-cli`), select your ESP32 board, install **NimBLE-Arduino** from the
Library Manager, and upload.

## Usage

On boot the firmware scans for the train, connects, subscribes to
notifications, and is ready. Press a button — the train reacts. Forward
and backward *arm* the throttle and start the motor at the current pot
magnitude (scaled to `[10..100]`); honk plays the horn sound; light
advances to the next colour in the LEGO-app palette.

While armed and the train is moving, twisting the pot retracks the
motor magnitude live — the loop watches the speedometer (port `0x36`)
and, when `|pot - |speed|| > 5`, resends the motor command. Direction
comes from the sign of the speedometer reading.

Stop disarms the throttle and sends motor 0, so the coast/brake runs
uninterrupted by pot tweaks. The throttle stays disarmed (pot ignored)
until Forward or Backward is pressed again. Manual pushes by hand are
not yet handled.

A small serial REPL is also exposed at 115200 baud for poking at the train
during development:

```
<int>        set motor speed, -100..100 (0 = stop)
honk         play horn
light        advance headlight to next palette colour
raw <hex>    send a raw LWP3 packet, e.g.  raw 08 00 81 32 11 51 00 2d
pi <port>    request port info (LWP3 0x21) for a port, hex
pm <p> <m>   request all mode info for port hex, mode decimal
```

---

# Protocol details

Most of what follows is reverse-engineered from sniffing the official LEGO
DUPLO Interactive Trains app via Android's Bluetooth HCI snoop log, plus
guesswork against the public
[LWP3 spec](https://lego.github.io/lego-ble-wireless-protocol-docs/). If
you're trying to talk to the 10428 from your own host (Python, Node, another
microcontroller), this is the part you want.

## Hub identification

The 10428 advertises as a generic LPF2 hub but with a *new* hub-type byte
that older libraries don't recognise. Example advertisement payload:

```
name = "DUPLO Train   "
mfg  = 97 03 00 21 02 ff 01 00
       ^^^^^      LEGO company ID (little-endian 0x0397)
             ^^   button state
                ^^ hub type = 0x21    <-- NEW (old DUPLO train hub = 0x20)
                   ^^ capabilities, then status bits
```

Legoino / node-poweredup filter on `mfg[3] == 0x20`. For the 10428 you have
to accept `0x21` as well. This firmware accepts either, so it should work
with both the old and the new train.

## BLE service and characteristic

Identical to every other LPF2 hub:

| Purpose | UUID |
|---|---|
| Service | `00001623-1212-efde-1623-785feabcd123` |
| Characteristic (write + notify) | `00001624-1212-efde-1623-785feabcd123` |

All commands are written to that one characteristic. Subscribe to
notifications on it to receive port announcements, sensor readings, and
command feedback.

## Connection sequence

1. **Scan** for advertisements that include the LPF2 service UUID
   (`0x1623…`) *and* whose manufacturer-data byte 3 is `0x20` or `0x21`.
2. **Connect** to that device.
3. **Discover** the service and characteristic above.
4. **Subscribe** to notifications on the characteristic. The train will
   then push a burst of *Hub-Attached-I/O* messages (`msg_type 0x04`)
   announcing its internal ports.
5. **Subscribe to EVENTS** on port `0x34` mode 1. This is required before
   the hub will accept sound/light writes on that port:

   ```
   0x0a 0x00 0x41 0x34 0x01 0x01 0x00 0x00 0x00 0x01
   ```

The train auto-disconnects within a couple of seconds if the central
doesn't subscribe to notifications, so don't dawdle. Also note that the
official Powered Up app on a nearby phone will steal the connection — kill
it (or just turn off the phone's Bluetooth) while developing.

## Port map

After subscribing, the hub announces its attached I/O. The 10428 layout:

| Port | Device type | Identity |
|---|---|---|
| `0x32` (50) | `0x29` = 41 | **Motor** (DUPLO_TRAIN_BASE_MOTOR, same ID as old hub) |
| `0x33` (51) | `0x5b` = 91 | TAG (write-only output; external writes get rejected with error 0x06 in practice) |
| `0x34` (52) | `0x5a` = 90 | **EVENTS** — sound, light, action-brick scans. 3 modes: 0=VERS, 1=EVENTS (2× u16 LE), 2=DEBUG |
| `0x35` (53) | `0x14` = 20 | Voltage sensor (battery), read-only u16 LE mV |
| `0x36` (54) | `0x2c` = 44 | Speedometer (same ID as old hub) |

The old DUPLO train hub used ports 0/1/18/19 and kept its beeper, headlight
RGB, and colour sensor on separate ports. The 10428 has renumbered ports
into the `0x3x` range and collapsed sound + light into a single
bidirectional EVENTS port (`0x34`, device type `0x5a`). Device types `0x5a`
and `0x5b` and hub variant `0x21` are not in the pybricks
`technical-info/assigned-numbers.md` table.

## LWP3 wire format

Every write to the characteristic is an LWP3 message:

```
[ length,        // total bytes including this one (1 byte while < 128)
  0x00,          // hub_id, always 0
  msg_type,      // 0x04 attached-IO, 0x41 input-format, 0x81 port-output, ...
  payload... ]
```

### Motor power

```
0x08 0x00 0x81 0x32 0x11 0x51 0x00 <speed>
 len  hub PortOut port start+fb WrDir mode  int8 -100..100   (0 = stop, 0x7f = brake)
```

- `0x81` = port output command
- `0x32` = motor port
- `0x11` = "execute immediately" + "request command feedback"
- `0x51` = write direct mode data
- `0x00` = mode index 0
- `<speed>` = signed 8-bit motor power. `0` stops; values in
  `-100..100` cruise; `0x7f` (127) is an electrical brake.

### Sound and light (EVENTS port `0x34` mode 1)

Both sound and headlight colour are written to the same port as a pair of
little-endian 16-bit values: `(opcode, parameter)`.

```
0x0b 0x00 0x81 0x34 0x11 0x51 0x01 <op_lo> <op_hi> <par_lo> <par_hi>
```

Opcodes recovered from the official app:

| Opcode | Parameter | Effect |
|---|---|---|
| `0x0107` | sound id (`0` = horn) | Play sound. *Side effect:* headlight briefly flashes yellow. |
| `0x0104` | colour index (u16 LE) | Set headlight colour. |
| `0xf001` | 0 | Session init. The LEGO app sends this once at connect; subscribing to EVENTS already seems sufficient and this firmware doesn't send it. |

Colour indices `0..10` follow the standard LWP3 colour table (`0` = off,
`9` = red, `10` = white, etc.). The official "next colour" button in the
LEGO app cycles through only a subset:
`11, 8 (orange), 7 (yellow), 9 (red), 10 (white), 0 (off), 1 (pink)`.
That's the same order `Train::cycleLights()` uses here. Index `11` is
clearly visible on the wire but its exact hue isn't documented in the LWP3
colour table.

### Inbound action-brick scan events

The DUPLO action bricks (horn, brake, station, water, steam) are colour-
coded and the train reads them with a downward-facing sensor. When the
train rolls over one, the hub pushes a *Port Value Single* notification on
port `0x34`:

```
notify: 08 00 45 34 <op_lo> <op_hi> <par_lo> <par_hi>
```

The horn brick produces opcode `0xa001`. Read-side opcodes are *not* the
same as write-side opcodes (writing `0x0107` plays the horn; reading
`0xa001` means the train just rolled over the horn brick). The full table
of brick → opcode mappings hasn't been catalogued yet; PRs welcome.

## Practical gotchas

- The ESP32 brownout detector can trip when the BLE radio first comes up on
  a marginal USB cable. Disable it with
  `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);` at the top of `setup()`
  (already done here). A better USB cable also helps.
- Random garbage at boot in the serial monitor is the ROM bootloader
  chattering at 74880 baud; harmless once you see `boot`.
- The train hub aggressively drops connections that don't subscribe to
  notifications quickly. "Connect" in nRF Connect alone isn't enough.
- The Powered Up / DUPLO Trains app on a nearby phone will silently take
  the connection if it gets there first.

## What this firmware doesn't (yet) do

- Read the **action-brick sensor** beyond noticing notifications arrive —
  no decoded mapping for brick → meaning.
- Drive the **hub status LED** (port not yet identified on the 10428).
- Query **battery** voltage or hub button events via Hub Properties.
- **Detect manual pushes** to arm the throttle. Right now the throttle
  only arms via the Forward/Backward buttons. Pushing the train by hand
  from a stopped state moves it but the pot stays inactive. Plan: when
  disarmed and the speedometer reports motion that persists past a
  short coast window, transition into the armed state with the sign of
  the speedometer as the direction.
- **Detect manual braking** (hand-stopping the train) as a disarm
  trigger. Currently the throttle stays armed indefinitely after Fwd /
  Back until Stop is pressed; if you grab the train and hold it the
  motor keeps fighting at pot magnitude. Plan: if speedometer reports 0
  for some debounce window while armed, auto-disarm.

## References

- LWP3 spec: <https://lego.github.io/lego-ble-wireless-protocol-docs/>
- node-poweredup (good read for protocol patterns, but does not support the
  10428): <https://github.com/nathankellenicki/node-poweredup>
- Legoino (also does not support the 10428):
  <https://github.com/corneliusmunz/legoino>

Working notes accumulated during bring-up live in
[`duplo-train-10428.md`](duplo-train-10428.md).

## License

MIT. Do whatever you want with this; attribution appreciated. LEGO and DUPLO
are trademarks of the LEGO Group, which does not sponsor or endorse this
project.

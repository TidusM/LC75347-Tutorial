# LC75347E audio controller

Control six analog audio channels with an Arduino Uno, two rotary encoders and
a 16x2 character LCD. Each channel has independent attenuation and mute; the
two front channels also have bass and treble controls. The Arduino sends
settings to the LC75347E over its CCB interface. Audio stays in the analog
circuit; this is not a surround decoder or an audio source.

## Hardware and wiring

Use the **LC75347E, QIP44M package**. IC pin numbers below are top-view pin
numbers, not Arduino pin numbers. Follow the manufacturer's application circuit
for the analog circuitry, coupling capacitors, tone networks, VREF and TIM:
[Sanyo LC75347E datasheet, ENN*7378](https://dtsheet.com/doc/147994/sanyo-lc75347e).

| Arduino Uno | Connection |
| --- | --- |
| D7 | LC75347E CL, pin 38 |
| D8 | LC75347E DI, pin 39 |
| D9 | LC75347E CE, pin 40 |
| D11 | LC75347E MUTE, pin 35; LOW=mute, HIGH=release |
| GND | LC75347E VSS, pin 41; encoder commons, button returns and LCD ground |
| D2 / D4 | Volume encoder A / B |
| D6 | Volume encoder push button |
| D3 / D5 | Settings encoder A / B |
| D12 | Settings encoder push button |
| A4 / SDA | LCD adapter SDA (Apply 2,2k to the VDD) |
| A5 / SCL | LCD adapter SCL (Apply 2,2k to the VDD) |

Power the processor at **5 V on VDD, pin 37**, with its supply decoupling.
VREF is an approximately half-supply reference, not ground; connect its 22 uF
capacitor to VSS as in the application circuit. Power the LCD and its adapter
at their specified voltage. The fitted LCD is a Winstar WH1602L-TMI-JT#,
16 characters by two lines, with an external I2C adapter.

Connect **Arduino D11 to LC75347E MUTE, pin 35**. This input is active LOW.
The controller uses both this hardware mute and the mute values in the serial
data. When unmuting, it sends the requested levels before releasing MUTE,
then sends them again after MUTE goes HIGH. Startup holds MUTE LOW during
initialization; Serial `r` also leaves it LOW until `u` is sent.

During reset/bootloader time, the Uno has not yet driven D11. Arrange the
external MUTE bias or amplifier sequencing for that interval; the sketch
cannot hold a GPIO LOW before it starts. Do not tie MUTE directly to the
supply when connecting it to the Arduino output.

Each encoder phase has its own **10 nF capacitor to ground**:
four capacitors across two encoders. Encoder and button inputs use internal
pull-ups. Identify A, B, common and button contacts on your actual encoder;
their physical arrangement varies.

| Channel | Input to volume stage | Output |
| --- | --- | --- |
| Front left | FLIN 7 | FLOUT 9 |
| Front right | FRIN 24 | FROUT 10 |
| Center | C 22 | COUT 11 |
| Rear left | RL 20 | RLOUT 12 |
| Rear right | RR 18 | RROUT 13 |
| Subwoofer | SBW 16 | SBWOUT 14 |

For front tone processing, feed FLTON 34 and FRTON 33. Connect the selector
outputs SELL 6 and SELR 25 through their external coupling capacitors to FLIN 7
and FRIN 24. The alternative FL 28 and FR 26 inputs bypass the tone section
when selected. Switching the menu setting does not move external wiring.

The 1 uF input-coupling capacitors in this circuit have their positive leads
toward the processor inputs. Follow the application circuit for output
coupling and other capacitor orientations; this is not a universal polarity
rule. Connect the coupled audio outputs to amplifier inputs, then the
amplifiers to their speakers. Amplifier supplies are separate requirements:
the example assembly uses 9.5 V amplifier boards, not the processor's 5 V rail.
Speaker return wiring depends on the amplifier topology.

Center, subwoofer and rears need their own audio inputs. A stereo source does
not become six-channel audio inside this chip. Four amplifiers can serve
FL/FR/center/subwoofer; the rear controls remain available for a six-amplifier
build. The level meter is optional and is not controlled by this sketch.

## Run

Open [LC75347_Controller.ino](LC75347_Controller/LC75347_Controller.ino) in the
Arduino IDE and select Arduino Uno. It uses the AVR core's built-in Wire
library, including its I2C timeout API. No additional LCD library is needed.
Power the processor before starting the controller; if powered later, send
`r` and then `u` after checking the settings.

**Startup plays at -30 dB**, all six channels enabled with zero offsets, flat
bass/treble, tone path on and zero-cross off. Keep amplifier gain low for the
first run. Settings are held in RAM; restarting restores these defaults.
The Serial `r` command resets to the same defaults but stays muted until `u`.

In [Config.h](LC75347_Controller/Config.h), match the adapter's I2C address
(default `0x3F`) and encoder direction/detent settings. The LCD driver assumes
P0=RS, P1=RW, P2=E, P3=backlight and P4..P7=LCD D4..D7. Backpacks with a
different mapping need a corresponding driver change. An I2C acknowledgement
alone does not verify this mapping. Display errors stop LCD writes while the
audio controls continue; `lcd` retries initialization.

## Controls

- Turn Volume to change the master attenuation; press it to mute/unmute.
- Turn Settings to browse; press to edit, turn to adjust, press to accept.
- Hold Settings to cancel the current edit and return home.

The menu contains Bass, Treble, Balance, Tone path, Zero cross and the six
individual channel levels. It wraps after SBW level. `OFF` mutes one channel.
Master changes preserve channel offsets and enabled states. Balance attenuates
the opposite front channel; positive values move the balance right.

## Serial commands

Use 115200 baud with a newline, CR or CRLF line ending. Values are decimal;
positive level numbers specify attenuation in dB. Commands do not report IC
readback: the printed state is what the controller requested.

| Command | Action |
| --- | --- |
| `master 30` | Set master attenuation, retaining offsets and enabled channels |
| `v 30` | Set all channels to 30 dB attenuation; clear offsets and balance |
| `v 96` | Turn all channels off |
| `c fl 40` | Set one channel's attenuation; names: `fl fr c rl rr sbw` |
| `c fl 96` | Turn one channel off |
| `m` / `u` | Hardware and data mute / unmute, retaining requested levels |
| `b 4` / `t -2` | Bass / treble, -12 to +12 dB in steps of 2 |
| `bal 6` | Front balance, -24 to +24 |
| `p 0` / `p 1` | Select direct front inputs / tone path |
| `z 0` / `z 1` | Disable / enable zero-cross switching |
| `o 5` | Set output-port sink mask 0..15, bit 0=OUTP1 through bit 3=OUTP4 |
| `r` | Reset the controller to its muted initial state |
| `s` / `h` | Show requested settings / command help |
| `lcd` | Retry the display after an I2C error |

OUTP1..4 are open-drain ports, not audio outputs. Their absolute maximum is
1 mA per output; limit an attached LED's current below that rating.

## License

Source code is available under the [MIT license](LICENSE).

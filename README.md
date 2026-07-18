# Frothy Synth

Frothy Synth is an experimental ESP32 audio spike built on
[AMY](https://github.com/shorepine/amy). AMY renders audio in C on a FreeRTOS
task pinned to core 1; Frothy schedules copied scalar events from the serial
prompt.

This is not a release. It exists to measure firmware size, RAM, timing,
Bluetooth/Wi-Fi coexistence, and start/stop behavior on physical hardware.

## Current slice

- 44.1 kHz, stereo, 16-bit standard I2S output.
- One global engine with 1-32 oscillators selected at startup.
- Reverb, echo, chorus, partials, custom voices, MIDI, and audio input disabled.
- Immediate or timestamped sine oscillators only.
- ESP32 DevKit V1 and Seeed XIAO ESP32S3 build targets.

AMY 1.2.72 is vendored from commit
`720f7707bb1c73fbfdb7f4e6a0fd8745a418dbd8` under `vendor/amy`. Its MIT
license is included there; Frothy Synth is also MIT-licensed. Frothy's vendor
changes avoid AMY's 16 KiB SysEx allocation when MIDI is disabled and replace
its built-in PCM samples, piano partials, and saw tables with inert stubs that
this first surface cannot reach.
This build also hard-clips instead of carrying AMY's soft-clipping lookup table.
Its stop path releases AMY's queue mutex and patch bookkeeping so the engine can
be restarted without those two upstream leaks.
The hot path remains in IRAM on ESP32S3; the classic ESP32 renders from flash
because Frothy's BLE-enabled image has insufficient IRAM, so its jitter must be
measured on hardware.

## Wire an I2S DAC

Connect three free output-capable GPIO pins to the DAC's BCLK, WS/LRCLK, and
DIN pins. Share ground and power the DAC according to its own requirements.
The current bridge does not emit MCLK. I2C is only needed if the chosen codec
has configuration registers.

## Build

The C extension must be built into firmware:

```sh
FROTHY_SOURCE_ROOT=/path/to/Frothy/core frothy build --project example
frothy flash seeed_xiao_esp32s3 --port /dev/cu.usbmodemXXXX
frothy install --project example --port /dev/cu.usbmodemXXXX
frothy connect --port /dev/cu.usbmodemXXXX
```

## Play one oscillator

At the Frothy serial prompt, substitute the GPIO pins you wired. Arguments are
BCLK, WS/LRCLK, data out, and oscillator capacity:

```frothy
synth.start: 7, 8, 9, 8
synth.sine: 0, 440, 200
synth.sine: 0, 440, 0
synth.stop:
```

Level is an integer from 0 to 1000. Start quietly.
`synth.running?` reports whether the audio task is still alive.

For scheduled playback, `synth.now` returns AMY's sample-derived clock in
milliseconds and `synth.sine-at` accepts that clock as its first argument:

```frothy
to play-later [ here start is synth.now:; set start to start + 100; synth.sine-at: start, 0, 440, 200; synth.sine-at: start + 500, 0, 440, 0 ]
play-later:
```

The visible clock reaches Frothy's positive integer limit after about 12.4
days. Stop and restart the engine before then; `synth.now` returns a range error
instead of silently wrapping into AMY's larger clock. AMY currently applies
scheduled changes at render-block boundaries; the default 256-sample block is
about 5.8 ms.

## Measure the spike

Before and after `synth.start`, run `:mem heap` and record `heap.free` and
`heap.largest`. Then check:

- 8, 16, and 32 oscillator capacities.
- audible glitches while editing Frothy and while BLE/Wi-Fi are active.
- ten start/play/stop cycles with heap returning to its starting value.
- scheduled onset timing at the analog or I2S output.

AMY's startup allocations are not yet reported safely to Frothy. An
out-of-memory start can reset the board. Do not treat this spike as a usable
instrument library until checked startup and the hardware measurements exist.

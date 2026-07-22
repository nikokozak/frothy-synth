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
its built-in PCM samples, piano partials, saw tables, and 391-entry factory
patch bank (~137 KiB of flash strings) with inert stubs that this first
surface cannot reach.
This build also hard-clips instead of carrying AMY's soft-clipping lookup table.
Its stop path releases AMY's queue mutex and patch bookkeeping so the engine can
be restarted without those two upstream leaks; filters_init is additionally
re-entrant (upstream leaks ~1 KiB per engine restart), and the delta pool's
first block is 256 entries instead of 2048 so startup fits the classic ESP32's
largest free heap block.
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
cd /path/to/Frothy/core/build/<board> && \
  python -m esptool --chip <chip> -p /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset write_flash "@flash_args"
frothy install --project example --port /dev/cu.usbmodemXXXX
frothy connect --port /dev/cu.usbmodemXXXX
```

Do not flash with `frothy flash <board>`: from a source checkout it rebuilds
the checkout kernel-only, replacing the project image you just built, and the
board ends up without the synth natives (`synth.start` reports `not found`).
Flash the project build directly with esptool as above (the build prints the
exact command).

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

## Measured on hardware (ESP32 DevKit V1, 2026-07-22)

Before and after `synth.start`, run `mem heap` and record `heap.free` and
`heap.largest`. Results from the first hardware pass:

- Flash: the library adds ~103 KiB to the image. With the default profile
  (BLE + Wi-Fi on) the image is ~1,407 KiB (8% app-partition headroom); with
  both radios gated off it is ~536 KiB (65% free).
- Heap: `synth.start` costs ~66 KiB (16 KiB task stack, 5 KiB delta pool,
  ~6.5 KiB I2S DMA, the rest AMY). Capacity 8/16/32 costs nearly the same at
  start — oscillators allocate lazily on first use. With radios on, ~30 KiB
  remains free while running; with radios off, ~103 KiB.
- Stability: 20 start/stop cycles with zero heap drift; scheduled two-
  oscillator playback via `synth.sine-at` works.

Still unmeasured: audible glitches while editing and while BLE/Wi-Fi are
active, jitter of the flash-resident render path, and scheduled onset timing
at the output — these need a DAC, ears, and a scope.

AMY's startup allocations are still not reported safely to Frothy: a start
that cannot fit fails with a capacity error when task creation fails, but an
allocation failure inside AMY itself can abort. Treat oscillator counts near
the heap limit with care.

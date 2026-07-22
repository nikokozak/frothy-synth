# Frothy Synth

A polyphonic subtractive synthesizer for Frothy on ESP32-class boards, built
on [AMY](https://github.com/shorepine/amy). One engine, up to 32 oscillators,
44.1 kHz stereo 16-bit audio over I2S. Every note is either immediate or
scheduled against a sample-derived millisecond clock (block resolution,
~5.8 ms), so melodies and sequences are ordinary Frothy words.

Boards: `esp32_devkit_v1`, `seeed_xiao_esp32s3`. Dual-core only — the audio
task owns core 1.

## Model

- `synth.start` claims three GPIO pins and starts the engine with a fixed
  oscillator capacity. Everything below needs a running engine.
- An **oscillator is a voice**: a wave, a frequency, a level, and optionally
  an envelope and a filter. Envelope and filter settings persist on their
  oscillator until `synth.stop`; changing them while a note sounds takes
  effect live.
- **level > 0 is note-on, level 0 is note-off.** Note-off runs the
  envelope's release; without an envelope, notes gate instantly (they click —
  set an envelope for musical notes).
- **Time** is `synth.now` milliseconds since start. The `-at` words take an
  absolute timestamp; `0` (or any past time) plays immediately. Events apply
  at render-block boundaries (~5.8 ms). The clock errors instead of wrapping
  after ~12.4 days — stop and restart before then.
- **Mixing**: oscillator outputs sum, then hard-clip. Keep the sum of active
  levels near 1000 or below; clipping sounds harsh, not loud.

## Words

| Word | Arguments | Valid |
|---|---|---|
| `synth.start` | bclk, ws, dout, oscs | distinct output GPIOs; oscs 1–32 |
| `synth.stop` | — | — |
| `synth.running?` | — | — |
| `synth.now` | — | — |
| `synth.sine` | osc, hz, level | plays now |
| `synth.wave` | osc, wave, hz, level | plays now |
| `synth.sine-at` | t, osc, hz, level | t ≥ 0 ms |
| `synth.wave-at` | t, osc, wave, hz, level | t ≥ 0 ms |
| `synth.envelope` | osc, attack, decay, sustain, release | ms ≤ 60000 each; sustain 0–1000 |
| `synth.filter` | osc, type, hz, resonance | type 0–3; resonance 0–8000 |

Everywhere: osc 0..capacity-1, hz 1–20000 (integer), level 0–1000 (linear
amplitude). Waves: **0** sine, **1** triangle, **2** saw, **3** pulse,
**4** noise (hz is ignored musically but still validated). Filter types:
**0** off, **1** low-pass, **2** band-pass, **3** high-pass; resonance is
milli-Q — 700 is neutral, 2000+ rings.

Errors: out-of-range arguments return `bad value`; any word before
`synth.start` returns `invalid`; a start that cannot fit in RAM returns
`capacity exceeded` and leaves the board running. If `synth.stop` ever
returns `io error` (audio task missed its deadline), call `synth.stop:`
again — the retry completes the teardown. Scheduled events are queued in
RAM (~20 bytes each): scheduling more than the heap holds drops the excess
with a `deltas:` line on serial and keeps playing — hundreds of pending
events are fine, thousands are not. `synth.stop` reclaims everything.

## Examples

Beep — A440 for half a second:

```frothy
synth.start: 26, 25, 22, 8
synth.sine: 0, 440, 200
synth.sine: 0, 440, 0        -- after a pause; level 0 = note-off
```

Chord — three voices, budgeted to stay under the clip ceiling:

```frothy
to chord [ synth.sine: 0, 262, 250; synth.sine: 1, 330, 250; synth.sine: 2, 392, 250 ]
to chord-off [ synth.sine: 0, 262, 0; synth.sine: 1, 330, 0; synth.sine: 2, 392, 0 ]
```

Pluck — a saw with a fast decay to silence; no note-off needed because
sustain is 0:

```frothy
synth.envelope: 0, 5, 250, 0, 100
to pluck with hz [ synth.wave: 0, 2, hz, 500 ]
pluck: 220
```

Acid bass — resonant low-pass on a saw, one scheduled bar; every timestamp
is arithmetic on one `synth.now` read:

```frothy
synth.envelope: 0, 5, 120, 400, 80
synth.filter: 0, 1, 900, 2500
to note with t, hz [ synth.wave-at: t, 0, 2, hz, 500; synth.wave-at: t + 110, 0, 2, hz, 0 ]
to bar [ here t is synth.now:; note: t, 110; note: t + 250, 110; note: t + 500, 220; note: t + 750, 138 ]
bar:
```

Two voices, phased — a bass line under a noise hat, scheduled together;
call `phrase:` again before a second elapses to keep it rolling:

```frothy
synth.envelope: 1, 1, 60, 0, 30
to hat with t [ synth.wave-at: t, 1, 4, 8000, 150 ]
to phrase [ here t is synth.now:; note: t, 55; note: t + 500, 82; hat: t; hat: t + 250; hat: t + 500; hat: t + 750 ]
```

## Wire an I2S DAC

Connect three free output-capable GPIO pins to the DAC's BCLK, WS/LRCLK, and
DIN pins; share ground. No MCLK is emitted. I2C is only needed if the codec
has configuration registers.

## Build and flash

```sh
FROTHY_SOURCE_ROOT=/path/to/Frothy/core frothy build --project example
cd /path/to/Frothy/core/build/<board> && \
  python -m esptool --chip <chip> -p /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset write_flash "@flash_args"
frothy install --project example --port /dev/cu.usbmodemXXXX
frothy connect --port /dev/cu.usbmodemXXXX
```

Do not flash with `frothy flash <board>`: from a source checkout it rebuilds
the checkout kernel-only, replacing the project image, and the board ends up
without the synth natives. Flash the project build directly with esptool (the
build prints the exact command).

## Measured (ESP32 DevKit V1)

- Flash: the library adds ~120 KiB. Against the 2 MiB app partition (core
  releases after the flash-floor raise), the whole image is ~554 KiB with
  BLE and Wi-Fi gated off (74% headroom) and ~1,425 KiB with both radios on
  (32%). On the older 1.5 MiB partition those become 64% and 7%.
- Heap: `synth.start` costs ~66 KiB (16 KiB task stack, 5 KiB event pool,
  ~6.5 KiB I2S DMA, the rest AMY). Capacity 8/16/32 costs nearly the same at
  start — voices allocate lazily on first use. Radios off leaves ~100 KiB
  free while running; radios on, ~30 KiB.
- Stability: repeated start/envelope/filter/play/stop cycles show zero heap
  drift; a start that cannot fit fails with `capacity exceeded`, not a reset.
  An allocation failure inside AMY itself can still abort — treat oscillator
  counts near the heap limit with care.

Unmeasured: audible glitch behavior while editing and while radios are
active, flash-write stalls (the render path pauses during NVS/code writes;
the ~30 ms of DMA buffering is the only cushion), and onset timing at the
output — these need a DAC, ears, and a scope.

## Vendor changes

AMY 1.2.72 is vendored from commit
`720f7707bb1c73fbfdb7f4e6a0fd8745a418dbd8` under `vendor/amy` (MIT, license
included; Frothy Synth is also MIT). Deviations from upstream, all measured
on hardware:

- PCM drum samples, piano partials, and the 391-entry factory patch bank
  (~137 KiB of flash strings) are stubbed — unreachable from this surface.
- The delta pool's first block is 256 events instead of 2048: upstream's
  40 KiB contiguous allocation starved the audio task stack on the classic
  ESP32.
- `filters_init` is re-entrant (upstream leaks ~1 KiB per engine restart)
  and the stop path releases AMY's queue mutex and patch bookkeeping (two
  more upstream restart leaks).
- 16 KiB SysEx buffers are skipped when MIDI is off; hard clipping replaces
  the soft-clip lookup table; the S3 keeps the render hot path in IRAM
  (the classic ESP32 renders from flash — see Unmeasured).

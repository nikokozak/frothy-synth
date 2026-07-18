/*
 * frothy-synth minimal AMY build
 *
 * The first Frothy surface can only create sine oscillators, so do not ship
 * AMY's built-in drum samples in the firmware image. Keep one inert entry so
 * the upstream PCM code remains linkable and fails silent if reached.
 */
#ifndef __PCM_H
#define __PCM_H

#define PCM_AMY_SAMPLE_RATE 22050
#define PCM_BASE_SAMPLES 0
#define PCM_BASE_LENGTH 1
#define PCM_WAVETABLE_BASE 0
#define PCM_WAVETABLE_SAMPLES 0
#define PCM_WAVETABLE_LEN 0
#define PCM_LENGTH 1
#define PCM_MAP_ENTRIES 1

const int16_t pcm[PCM_LENGTH] PROGMEM = {0};
const uint16_t pcm_samples = 0;
const uint16_t pcm_wavetable_base = PCM_WAVETABLE_BASE;
const uint16_t pcm_wavetable_samples = PCM_WAVETABLE_SAMPLES;
const uint32_t pcm_wavetable_len = PCM_WAVETABLE_LEN;
const pcm_map_t pcm_map[PCM_MAP_ENTRIES] PROGMEM = {{0, 0, 0, 0, 69}};

#endif /* __PCM_H */

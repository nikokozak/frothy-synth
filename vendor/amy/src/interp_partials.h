/*
 * frothy-synth minimal AMY build
 *
 * Interpolated partials are disabled in the bridge configuration. This inert
 * voice preserves AMY's link-time interface without embedding its piano bank.
 */
#ifndef __INTERP_PARTIALS_H
#define __INTERP_PARTIALS_H

#define NUM_INTERP_PARTIALS_PRESETS 1

static const uint16_t fr_partial_times[] PROGMEM = {10};
static const uint8_t fr_partial_velocities[] PROGMEM = {1, 127};
static const uint8_t fr_partial_pitches[] PROGMEM = {60, 72};
static const uint8_t fr_partial_counts[] PROGMEM = {0, 0, 0, 0};
static const uint16_t fr_partial_freqs[] PROGMEM = {0};
static const uint8_t fr_partial_mags[] PROGMEM = {0};

const interp_partials_voice_t
    interp_partials_map[NUM_INTERP_PARTIALS_PRESETS] PROGMEM = {{
        1,
        fr_partial_times,
        2,
        fr_partial_velocities,
        2,
        fr_partial_pitches,
        fr_partial_counts,
        fr_partial_freqs,
        fr_partial_mags,
    }};

#endif /* __INTERP_PARTIALS_H */
